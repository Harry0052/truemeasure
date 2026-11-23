#include "truemeasure/engine.hpp"

#include "truemeasure/sha256.hpp"

#include <opencv2/aruco.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace truemeasure {
namespace {

using json = nlohmann::json;

Result rejected(std::string category, std::string message) {
  Result result;
  result.status = Status::Rejected;
  result.category = std::move(category);
  result.message = std::move(message);
  result.assumptions = "No dimension is reported. A rejected input is not a measurement.";
  result.uncertainty_method = "not_applicable";
  return result;
}

Result withheld(std::string category, std::string message) {
  Result result;
  result.status = Status::Withheld;
  result.category = std::move(category);
  result.message = std::move(message);
  result.assumptions =
      "Dimensions are withheld. Any calibration notes are conditional on a planar board and the "
      "declared distortion model, and they are not a certified tolerance.";
  result.uncertainty_method =
      "deterministic_bounds_v1 was not published because the measurement was withheld";
  return result;
}

void copy_identity(Result &result, const Request &request) {
  result.board_id = request.board_id;
  result.image = request.image;
  result.request_sha256 = request.request_sha256;
  result.image_sha256 = request.image_sha256;
  result.board_sha256 = request.board_sha256;
  result.coplanar_attestation = request.coplanar_attestation;
  if (request.points.size() == 2) {
    result.point_a = {request.points[0].x, request.points[0].y};
    result.point_b = {request.points[1].x, request.points[1].y};
  }
}

std::filesystem::path resolve_against(const std::filesystem::path &base_file,
                                      const std::string &stored) {
  std::filesystem::path path(stored);
  if (path.is_absolute()) {
    return path.lexically_normal();
  }
  return (base_file.parent_path() / path).lexically_normal();
}

bool read_file_limited(const std::filesystem::path &path, std::uintmax_t limit, std::string &bytes,
                       std::string &error) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) {
    error = "not a regular file: " + path.string();
    return false;
  }
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) {
    error = "cannot stat " + path.string();
    return false;
  }
  if (size > limit) {
    error = "file exceeds " + std::to_string(limit) + " bytes: " + path.string();
    return false;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot open " + path.string();
    return false;
  }
  bytes.assign(size, '\0');
  if (size > 0) {
    in.read(bytes.data(), static_cast<std::streamsize>(size));
    if (!in) {
      error = "cannot read " + path.string();
      return false;
    }
  }
  return true;
}

bool is_png(const std::string &bytes) {
  static constexpr unsigned char kMagic[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
  return bytes.size() >= sizeof(kMagic) &&
         std::equal(std::begin(kMagic), std::end(kMagic),
                    reinterpret_cast<const unsigned char *>(bytes.data()));
}

bool is_jpeg(const std::string &bytes) {
  return bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xff &&
         static_cast<unsigned char>(bytes[1]) == 0xd8 &&
         static_cast<unsigned char>(bytes[2]) == 0xff;
}

double round_nearest_tenth(double value) {
  return std::round(value * 10.0) / 10.0;
}

double round_outward_low(double value) {
  return std::floor((value + 1e-9) * 10.0) / 10.0;
}

double round_outward_high(double value) {
  return std::ceil((value - 1e-9) * 10.0) / 10.0;
}

double round_hundredth(double value) {
  return std::round(value * 100.0) / 100.0;
}

bool finite_number(const json &value) {
  return value.is_number() && std::isfinite(value.get<double>());
}

struct LoadOk {
  Request request;
  Board board;
  cv::Mat image;
};

struct LoadFail {
  Result result;
};

std::string overflow_message(const std::exception &error) {
  return error.what();
}

Result parse_failure(const std::exception &error) {
  const std::string text = overflow_message(error);
  if (text.find("overflow") != std::string::npos ||
      text.find("out of range") != std::string::npos) {
    return rejected("nonfinite_point", "A coordinate or tolerance is not a finite JSON number (" +
                                           text + "). Replace it with a finite pixel coordinate.");
  }
  return rejected("schema_invalid", "The request or board JSON could not be parsed (" + text +
                                        "). Check the schema version and required fields.");
}

std::optional<double> required_finite(const json &object, const char *key, std::string &category,
                                      std::string &message) {
  if (!object.contains(key) || !object.at(key).is_number()) {
    category = "schema_invalid";
    message = std::string("Missing or non-numeric field: ") + key;
    return std::nullopt;
  }
  const double value = object.at(key).get<double>();
  if (!std::isfinite(value)) {
    category = "nonfinite_point";
    message = std::string(key) + " is not finite.";
    return std::nullopt;
  }
  return value;
}

bool load_board(const std::filesystem::path &path, const std::string &bytes, Board &board,
                std::string &category, std::string &message) {
  json parsed;
  try {
    parsed = json::parse(bytes);
  } catch (const std::exception &error) {
    category = "schema_invalid";
    message = std::string("Board JSON is invalid: ") + error.what();
    return false;
  }
  if (!parsed.is_object() || parsed.value("schema", "") != "truemeasure.board" ||
      parsed.value("schema_version", 0) != kSchemaVersion) {
    category = "schema_invalid";
    message = "Board file must be schema truemeasure.board version 1.";
    return false;
  }
  board.schema_version = kSchemaVersion;
  board.board_id = parsed.value("board_id", "");
  board.units = parsed.value("units", "");
  board.dictionary = parsed.value("dictionary", "");
  if (board.board_id.empty() || board.units != "millimetre" || board.dictionary != "DICT_4X4_50") {
    category = "schema_invalid";
    message =
        "Board must declare units millimetre, dictionary DICT_4X4_50, and a board id. Path: " +
        path.string();
    return false;
  }
  auto size = required_finite(parsed, "marker_size_mm", category, message);
  auto width = required_finite(parsed, "width_mm", category, message);
  auto height = required_finite(parsed, "height_mm", category, message);
  if (!size || !width || !height) {
    return false;
  }
  if (*size <= 0 || *width <= 0 || *height <= 0) {
    category = "schema_invalid";
    message = "Board size and marker size must be positive millimetres.";
    return false;
  }
  board.marker_size_mm = *size;
  board.width_mm = *width;
  board.height_mm = *height;
  if (!parsed.contains("markers") || !parsed["markers"].is_array() ||
      parsed["markers"].size() < 5) {
    category = "schema_invalid";
    message = "Board needs at least five markers so one can be withheld as a check.";
    return false;
  }
  std::set<int> seen;
  for (const auto &marker : parsed["markers"]) {
    if (!marker.is_object() || !marker.contains("id") || !marker["id"].is_number_integer()) {
      category = "schema_invalid";
      message = "Each marker needs an integer id.";
      return false;
    }
    auto x = required_finite(marker, "x_mm", category, message);
    auto y = required_finite(marker, "y_mm", category, message);
    if (!x || !y) {
      return false;
    }
    MarkerSpec spec;
    spec.id = marker["id"].get<int>();
    spec.x_mm = *x;
    spec.y_mm = *y;
    if (!seen.insert(spec.id).second) {
      category = "schema_invalid";
      message = "Duplicate marker id " + std::to_string(spec.id) + ".";
      return false;
    }
    if (spec.x_mm < 0 || spec.y_mm < 0 || spec.x_mm + board.marker_size_mm > board.width_mm ||
        spec.y_mm + board.marker_size_mm > board.height_mm) {
      category = "schema_invalid";
      message = "Marker " + std::to_string(spec.id) + " does not lie inside the board.";
      return false;
    }
    board.markers.push_back(spec);
  }
  return true;
}

bool load_request_json(const std::string &bytes, Request &request, std::string &category,
                       std::string &message) {
  json parsed;
  try {
    parsed = json::parse(bytes);
  } catch (const std::exception &error) {
    Result failure = parse_failure(error);
    category = failure.category;
    message = failure.message;
    return false;
  }
  if (!parsed.is_object() || parsed.value("schema", "") != "truemeasure.request" ||
      parsed.value("schema_version", 0) != kSchemaVersion) {
    category = "schema_invalid";
    message = "Request must be schema truemeasure.request version 1.";
    return false;
  }
  request.schema_version = kSchemaVersion;
  request.board_id = parsed.value("board_id", "");
  request.board_file = parsed.value("board_file", "");
  request.image = parsed.value("image", "");
  if (request.board_id.empty() || request.board_file.empty() || request.image.empty()) {
    category = "schema_invalid";
    message = "Request needs board_id, board_file, and image.";
    return false;
  }
  if (!parsed.contains("coplanar_attestation") || !parsed["coplanar_attestation"].is_boolean()) {
    category = "schema_invalid";
    message = "coplanar_attestation must be a boolean. Set it true only when both points and the "
              "board are on one plane.";
    return false;
  }
  request.coplanar_attestation = parsed["coplanar_attestation"].get<bool>();
  if (!parsed.contains("points_px") || !parsed["points_px"].is_array() ||
      parsed["points_px"].size() != 2) {
    category = "schema_invalid";
    message = "points_px must contain exactly two endpoints.";
    return false;
  }
  for (const auto &point : parsed["points_px"]) {
    if (!point.is_object() || !point.contains("id") || !point["id"].is_string() ||
        point["id"].get<std::string>().empty()) {
      category = "schema_invalid";
      message = "Each point needs a non-empty string id.";
      return false;
    }
    auto x = required_finite(point, "x", category, message);
    auto y = required_finite(point, "y", category, message);
    if (!x || !y) {
      if (category.empty()) {
        category = "nonfinite_point";
        message = "A point coordinate is not finite.";
      }
      return false;
    }
    request.points.push_back(RequestPoint{point["id"].get<std::string>(), *x, *y});
  }
  if (request.points[0].id == request.points[1].id) {
    category = "schema_invalid";
    message = "The two endpoints need distinct ids.";
    return false;
  }
  if (!parsed.contains("camera") || !parsed["camera"].is_object()) {
    category = "schema_invalid";
    message = "camera.model is required. Use \"none\" when distortion is zero.";
    return false;
  }
  request.camera.model = parsed["camera"].value("model", "");
  if (request.camera.model == "opencv_pinhole") {
    auto fx = required_finite(parsed["camera"], "fx", category, message);
    auto fy = required_finite(parsed["camera"], "fy", category, message);
    auto cx = required_finite(parsed["camera"], "cx", category, message);
    auto cy = required_finite(parsed["camera"], "cy", category, message);
    if (!fx || !fy || !cx || !cy) {
      return false;
    }
    if (*fx <= 1.0 || *fy <= 1.0) {
      category = "schema_invalid";
      message = "Camera focal lengths must be greater than 1 pixel.";
      return false;
    }
    request.camera.fx = *fx;
    request.camera.fy = *fy;
    request.camera.cx = *cx;
    request.camera.cy = *cy;
    if (!parsed["camera"].contains("distortion") || !parsed["camera"]["distortion"].is_array() ||
        parsed["camera"]["distortion"].size() != 5) {
      category = "schema_invalid";
      message = "opencv_pinhole distortion must be five coefficients [k1, k2, p1, p2, k3].";
      return false;
    }
    for (const auto &coeff : parsed["camera"]["distortion"]) {
      if (!finite_number(coeff)) {
        category = "nonfinite_point";
        message = "A distortion coefficient is not finite.";
        return false;
      }
      request.camera.distortion.push_back(coeff.get<double>());
    }
  } else if (request.camera.model != "none") {
    category = "schema_invalid";
    message = "Unsupported camera model. Use none or opencv_pinhole.";
    return false;
  }
  auto scale = required_finite(parsed, "scale_tolerance_mm", category, message);
  auto click = required_finite(parsed, "endpoint_click_tolerance_px", category, message);
  auto marker = required_finite(parsed, "marker_corner_tolerance_px", category, message);
  if (!scale || !click || !marker) {
    return false;
  }
  if (*scale < 0 || *click < 0 || *marker < 0 || *scale > kMaxScaleToleranceMm ||
      *click > kMaxClickTolerancePx || *marker > kMaxMarkerTolerancePx) {
    category = "schema_invalid";
    message = "Tolerances must be finite and inside the supported bounds (scale <= 25 mm, click "
              "<= 40 px, marker corner <= 5 px).";
    return false;
  }
  request.scale_tolerance_mm = *scale;
  request.click_tolerance_px = *click;
  request.marker_corner_tolerance_px = *marker;
  return true;
}

struct Loaded {
  bool ok = false;
  LoadOk value;
  Result error;
};

Loaded load_all(const std::filesystem::path &request_path) {
  Loaded loaded;
  std::string request_bytes;
  std::string io_error;
  if (!read_file_limited(request_path, kMaxJsonBytes, request_bytes, io_error)) {
    const bool oversized = io_error.find("exceeds") != std::string::npos;
    loaded.error = rejected(oversized ? "image_oversized" : "schema_invalid", io_error);
    if (oversized) {
      loaded.error.category = "schema_invalid";
      loaded.error.message = "Request JSON exceeds the 1 MiB limit.";
    }
    return loaded;
  }
  Request request;
  std::string category;
  std::string message;
  try {
    request.request_sha256 = sha256_hex(
        reinterpret_cast<const std::uint8_t *>(request_bytes.data()), request_bytes.size());
  } catch (const std::exception &error) {
    loaded.error = rejected("internal", error.what());
    loaded.error.status = Status::Internal;
    return loaded;
  }
  if (!load_request_json(request_bytes, request, category, message)) {
    loaded.error = rejected(category, message);
    loaded.error.request_sha256 = request.request_sha256;
    return loaded;
  }
  const auto board_path = resolve_against(request_path, request.board_file);
  const auto image_path = resolve_against(request_path, request.image);
  std::string board_bytes;
  if (!read_file_limited(board_path, kMaxJsonBytes, board_bytes, io_error)) {
    loaded.error = rejected("schema_invalid", "Cannot read board file. " + io_error);
    loaded.error.request_sha256 = request.request_sha256;
    loaded.error.board_id = request.board_id;
    loaded.error.image = request.image;
    return loaded;
  }
  request.board_sha256 =
      sha256_hex(reinterpret_cast<const std::uint8_t *>(board_bytes.data()), board_bytes.size());
  Board board;
  if (!load_board(board_path, board_bytes, board, category, message)) {
    loaded.error = rejected(category, message);
    copy_identity(loaded.error, request);
    return loaded;
  }
  if (board.board_id != request.board_id) {
    loaded.error =
        rejected("schema_invalid", "Request board_id does not match the board file (" +
                                       request.board_id + " vs " + board.board_id + ").");
    copy_identity(loaded.error, request);
    return loaded;
  }
  std::string image_bytes;
  if (!read_file_limited(image_path, kMaxImageBytes, image_bytes, io_error)) {
    if (io_error.find("exceeds") != std::string::npos) {
      loaded.error =
          rejected("image_oversized",
                   "Image file exceeds the 16 MiB limit and was not decoded. " + io_error);
    } else {
      loaded.error = rejected("image_malformed", "Cannot read the image. " + io_error);
    }
    copy_identity(loaded.error, request);
    return loaded;
  }
  request.image_sha256 =
      sha256_hex(reinterpret_cast<const std::uint8_t *>(image_bytes.data()), image_bytes.size());
  if (!is_png(image_bytes) && !is_jpeg(image_bytes)) {
    loaded.error =
        rejected("image_malformed",
                 "Image must be a PNG or JPEG. The file magic bytes were not recognized.");
    copy_identity(loaded.error, request);
    return loaded;
  }
  const std::vector<unsigned char> encoded(image_bytes.begin(), image_bytes.end());
  cv::Mat image = cv::imdecode(encoded, cv::IMREAD_COLOR);
  if (image.empty()) {
    loaded.error = rejected("image_malformed", "The image file could not be decoded.");
    copy_identity(loaded.error, request);
    return loaded;
  }
  const long long pixels = static_cast<long long>(image.cols) * static_cast<long long>(image.rows);
  if (image.cols > kMaxEdgePx || image.rows > kMaxEdgePx || pixels > kMaxPixels) {
    loaded.error = rejected("image_oversized",
                            "Decoded image exceeds the limit of " + std::to_string(kMaxEdgePx) +
                                " pixels on a side and " + std::to_string(kMaxPixels) + " pixels.");
    copy_identity(loaded.error, request);
    return loaded;
  }
  if (image.cols < 32 || image.rows < 32) {
    loaded.error =
        rejected("image_malformed", "Decoded image is smaller than 32 pixels on a side.");
    copy_identity(loaded.error, request);
    return loaded;
  }
  for (const auto &point : request.points) {
    if (point.x < 0 || point.y < 0 || point.x >= image.cols || point.y >= image.rows) {
      loaded.error =
          rejected("point_outside_image",
                   "Endpoint '" + point.id +
                       "' is outside the decoded image. Choose both points inside the photograph.");
      copy_identity(loaded.error, request);
      return loaded;
    }
  }
  loaded.ok = true;
  loaded.value.request = std::move(request);
  loaded.value.board = std::move(board);
  loaded.value.image = std::move(image);
  return loaded;
}

std::vector<cv::Point2f> correct_points(const std::vector<cv::Point2f> &points,
                                        const CameraModel &camera) {
  if (camera.model != "opencv_pinhole" || points.empty()) {
    return points;
  }
  bool nonzero = false;
  for (double coeff : camera.distortion) {
    if (std::fabs(coeff) > 1e-15) {
      nonzero = true;
    }
  }
  if (!nonzero) {
    return points;
  }
  cv::Mat intrinsics =
      (cv::Mat_<double>(3, 3) << camera.fx, 0, camera.cx, 0, camera.fy, camera.cy, 0, 0, 1);
  cv::Mat distortion(1, static_cast<int>(camera.distortion.size()), CV_64F);
  for (int i = 0; i < distortion.cols; ++i) {
    distortion.at<double>(0, i) = camera.distortion[static_cast<std::size_t>(i)];
  }
  std::vector<cv::Point2f> corrected;
  cv::undistortPoints(points, corrected, intrinsics, distortion, cv::noArray(), intrinsics);
  return corrected;
}

cv::Point2f distort_pixel(cv::Point2f pixel, const CameraModel &camera) {
  if (camera.model != "opencv_pinhole" || camera.distortion.size() < 5) {
    return pixel;
  }
  const double x = (pixel.x - camera.cx) / camera.fx;
  const double y = (pixel.y - camera.cy) / camera.fy;
  const double r2 = x * x + y * y;
  const double k1 = camera.distortion[0];
  const double k2 = camera.distortion[1];
  const double p1 = camera.distortion[2];
  const double p2 = camera.distortion[3];
  const double k3 = camera.distortion[4];
  const double radial = 1.0 + k1 * r2 + k2 * r2 * r2 + k3 * r2 * r2 * r2;
  const double xd = x * radial + 2.0 * p1 * x * y + p2 * (r2 + 2.0 * x * x);
  const double yd = y * radial + p1 * (r2 + 2.0 * y * y) + 2.0 * p2 * x * y;
  return cv::Point2f(static_cast<float>(xd * camera.fx + camera.cx),
                     static_cast<float>(yd * camera.fy + camera.cy));
}

double polygon_area(const std::vector<cv::Point2f> &hull) {
  if (hull.size() < 3) {
    return 0;
  }
  return std::fabs(cv::contourArea(hull));
}

double minor_extent(const std::vector<cv::Point2d> &points) {
  if (points.size() < 2) {
    return 0;
  }
  cv::Mat data(static_cast<int>(points.size()), 2, CV_64F);
  for (int i = 0; i < data.rows; ++i) {
    data.at<double>(i, 0) = points[static_cast<std::size_t>(i)].x;
    data.at<double>(i, 1) = points[static_cast<std::size_t>(i)].y;
  }
  cv::PCA pca(data, cv::Mat(), cv::PCA::DATA_AS_ROW);
  if (pca.eigenvectors.empty() || pca.eigenvectors.rows < 1) {
    return 0;
  }
  const int row = pca.eigenvectors.rows - 1;
  cv::Point2d axis(pca.eigenvectors.at<double>(row, 0), pca.eigenvectors.at<double>(row, 1));
  const double norm = std::hypot(axis.x, axis.y);
  if (norm < 1e-12) {
    return 0;
  }
  axis.x /= norm;
  axis.y /= norm;
  const double mx = pca.mean.at<double>(0, 0);
  const double my = pca.mean.at<double>(0, 1);
  double low = std::numeric_limits<double>::infinity();
  double high = -low;
  for (const auto &point : points) {
    const double t = (point.x - mx) * axis.x + (point.y - my) * axis.y;
    low = std::min(low, t);
    high = std::max(high, t);
  }
  return high - low;
}

struct Correspondence {
  int id = 0;
  std::array<cv::Point2f, 4> raw_px{};
  std::array<cv::Point2f, 4> corrected_px{};
  std::array<cv::Point2d, 4> board_mm{};
};

bool homography_ok(const cv::Mat &homography) {
  if (homography.empty() || homography.rows != 3 || homography.cols != 3) {
    return false;
  }
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      if (!std::isfinite(homography.at<double>(r, c))) {
        return false;
      }
    }
  }
  return true;
}

std::optional<double> distance_with(const std::vector<cv::Point2f> &image_points,
                                    const std::vector<cv::Point2d> &board_points, cv::Point2f a,
                                    cv::Point2f b, cv::Mat *homography_out) {
  if (image_points.size() < 4 || image_points.size() != board_points.size()) {
    return std::nullopt;
  }
  cv::Mat homography = cv::findHomography(image_points, board_points, 0);
  if (!homography_ok(homography)) {
    return std::nullopt;
  }
  std::vector<cv::Point2f> endpoints{a, b};
  std::vector<cv::Point2f> mapped;
  cv::perspectiveTransform(endpoints, mapped, homography);
  if (mapped.size() != 2 || !std::isfinite(mapped[0].x) || !std::isfinite(mapped[0].y) ||
      !std::isfinite(mapped[1].x) || !std::isfinite(mapped[1].y)) {
    return std::nullopt;
  }
  if (homography_out != nullptr) {
    *homography_out = homography;
  }
  return std::hypot(static_cast<double>(mapped[1].x - mapped[0].x),
                    static_cast<double>(mapped[1].y - mapped[0].y));
}

struct Residual {
  double rms = 0;
  double max = 0;
};

Residual residuals_of(const cv::Mat &homography, const std::vector<cv::Point2f> &image_points,
                      const std::vector<cv::Point2d> &board_points) {
  std::vector<cv::Point2f> projected;
  cv::perspectiveTransform(image_points, projected, homography);
  Residual residual;
  double sum = 0;
  for (std::size_t i = 0; i < projected.size(); ++i) {
    const double dx = projected[i].x - board_points[i].x;
    const double dy = projected[i].y - board_points[i].y;
    const double error = std::hypot(dx, dy);
    sum += error * error;
    residual.max = std::max(residual.max, error);
  }
  residual.rms = std::sqrt(sum / static_cast<double>(projected.size()));
  return residual;
}

Result measure_loaded(const Request &request, const Board &board, const cv::Mat &image) {
  Result result = withheld("internal", "");
  copy_identity(result, request);
  result.uncertainty_method =
      "deterministic_bounds_v1: nominal homography; endpoints shifted by "
      "±endpoint_click_tolerance_px along the image segment and its perpendicular, plus cardinal "
      "shifts of each endpoint; marker corners scaled radially by ±marker_corner_tolerance_px "
      "about their centroid; the distance is then expanded by ±scale_tolerance_mm. The published "
      "interval is the outward-rounded min and max. It is conditional on the planar attestation "
      "and the declared distortion model. It is not a statistical confidence interval.";
  result.assumptions =
      "Both endpoints are coplanar with the printed board only if the operator attested that. "
      "Lens distortion is whatever the request declares. Residual uncertainty is conditional on "
      "those statements.";

  if (!request.coplanar_attestation) {
    Result held = withheld(
        "coplanarity_not_attested",
        "The operator did not attest that both points lie on the same plane as the reference "
        "board. No dimension is reported. Set coplanar_attestation to true only after that check, "
        "then run the measurement again.");
    copy_identity(held, request);
    held.uncertainty_method = result.uncertainty_method;
    held.assumptions = result.assumptions;
    return held;
  }

  std::map<int, MarkerSpec> by_id;
  for (const auto &marker : board.markers) {
    by_id.emplace(marker.id, marker);
  }
  auto dictionary = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
  auto parameters = cv::aruco::DetectorParameters::create();
  parameters->cornerRefinementMethod = cv::aruco::CORNER_REFINE_SUBPIX;
  std::vector<std::vector<cv::Point2f>> corners;
  std::vector<int> ids;
  cv::aruco::detectMarkers(image, dictionary, corners, ids, parameters);

  std::vector<Correspondence> detected;
  std::set<int> seen;
  for (std::size_t i = 0; i < ids.size(); ++i) {
    const auto found = by_id.find(ids[i]);
    if (found == by_id.end() || corners[i].size() != 4) {
      continue;
    }
    if (!seen.insert(ids[i]).second) {
      Result held = withheld("markers_degenerate",
                             "The same marker id was detected twice. No dimension is reported.");
      copy_identity(held, request);
      return held;
    }
    Correspondence item;
    item.id = ids[i];
    const double origin_x = found->second.x_mm;
    const double origin_y = found->second.y_mm;
    const double size = board.marker_size_mm;
    const cv::Point2d local[4] = {{0, 0}, {size, 0}, {size, size}, {0, size}};
    std::vector<cv::Point2f> raw(corners[i].begin(), corners[i].end());
    const std::vector<cv::Point2f> corrected = correct_points(raw, request.camera);
    for (int k = 0; k < 4; ++k) {
      item.raw_px[static_cast<std::size_t>(k)] = raw[static_cast<std::size_t>(k)];
      item.corrected_px[static_cast<std::size_t>(k)] = corrected[static_cast<std::size_t>(k)];
      item.board_mm[static_cast<std::size_t>(k)] =
          cv::Point2d(origin_x + local[k].x, origin_y + local[k].y);
    }
    detected.push_back(item);
  }
  std::sort(detected.begin(), detected.end(),
            [](const Correspondence &a, const Correspondence &b) { return a.id < b.id; });
  for (const auto &item : detected) {
    result.detected_ids.push_back(item.id);
    result.marker_corners_px.push_back({PixelPoint{item.raw_px[0].x, item.raw_px[0].y},
                                        PixelPoint{item.raw_px[1].x, item.raw_px[1].y},
                                        PixelPoint{item.raw_px[2].x, item.raw_px[2].y},
                                        PixelPoint{item.raw_px[3].x, item.raw_px[3].y}});
  }
  if (static_cast<int>(detected.size()) < kMinMarkers) {
    Result held = withheld(
        "markers_insufficient",
        "Only " + std::to_string(detected.size()) +
            " board markers were detected; at least 4 are required. No dimension is reported. "
            "Reframe the photo so the reference markers are visible, sharp, and unobstructed.");
    held.detected_ids = result.detected_ids;
    held.marker_corners_px = result.marker_corners_px;
    copy_identity(held, request);
    return held;
  }

  std::vector<cv::Point2f> all_board_f;
  std::vector<cv::Point2d> all_board_d;
  for (const auto &item : detected) {
    for (int k = 0; k < 4; ++k) {
      all_board_f.emplace_back(static_cast<float>(item.board_mm[k].x),
                               static_cast<float>(item.board_mm[k].y));
      all_board_d.push_back(item.board_mm[k]);
    }
  }
  std::vector<cv::Point2f> hull;
  cv::convexHull(all_board_f, hull);
  result.hull_area_mm2 = round_hundredth(polygon_area(hull));
  result.minor_extent_mm = round_hundredth(minor_extent(all_board_d));
  if (*result.hull_area_mm2 < kMinHullAreaMm2 || *result.minor_extent_mm < kMinMinorExtentMm) {
    Result held =
        withheld("markers_ill_spread",
                 "Detected markers cover " + format_fixed(*result.hull_area_mm2, 1) +
                     " mm^2 with a minor spread of " + format_fixed(*result.minor_extent_mm, 1) +
                     " mm. That is too small or too close to a single line to support a plane. No "
                     "dimension is reported. Include markers from opposite sides of the board.");
    held.detected_ids = result.detected_ids;
    held.marker_corners_px = result.marker_corners_px;
    held.hull_area_mm2 = result.hull_area_mm2;
    held.minor_extent_mm = result.minor_extent_mm;
    copy_identity(held, request);
    return held;
  }
  if (static_cast<int>(detected.size()) < kMinMarkersWithCheck) {
    Result held = withheld(
        "markers_no_redundant_check",
        "Four markers fix a homography, but none is left over to check it. No dimension is "
        "reported. Use a view where at least one extra board marker is visible.");
    held.detected_ids = result.detected_ids;
    held.marker_corners_px = result.marker_corners_px;
    held.hull_area_mm2 = result.hull_area_mm2;
    held.minor_extent_mm = result.minor_extent_mm;
    copy_identity(held, request);
    return held;
  }

  const int check_id =
      std::max_element(detected.begin(), detected.end(),
                       [](const Correspondence &a, const Correspondence &b) { return a.id < b.id; })
          ->id;
  std::vector<cv::Point2f> fit_image;
  std::vector<cv::Point2d> fit_board;
  std::vector<cv::Point2f> check_image;
  std::vector<cv::Point2d> check_board;
  for (const auto &item : detected) {
    auto &image_dest = item.id == check_id ? check_image : fit_image;
    auto &board_dest = item.id == check_id ? check_board : fit_board;
    if (item.id == check_id) {
      result.check_ids.push_back(item.id);
    } else {
      result.fit_ids.push_back(item.id);
    }
    for (int k = 0; k < 4; ++k) {
      image_dest.push_back(item.corrected_px[static_cast<std::size_t>(k)]);
      board_dest.push_back(item.board_mm[static_cast<std::size_t>(k)]);
    }
  }
  if (static_cast<int>(result.fit_ids.size()) < kMinMarkers) {
    Result held = withheld("markers_insufficient",
                           "Withholding the check marker left fewer than 4 markers to fit. No "
                           "dimension is reported.");
    held.detected_ids = result.detected_ids;
    held.marker_corners_px = result.marker_corners_px;
    held.fit_ids = result.fit_ids;
    held.check_ids = result.check_ids;
    copy_identity(held, request);
    return held;
  }

  std::vector<cv::Point2f> raw_endpoints{
      {static_cast<float>(request.points[0].x), static_cast<float>(request.points[0].y)},
      {static_cast<float>(request.points[1].x), static_cast<float>(request.points[1].y)}};
  const std::vector<cv::Point2f> endpoints = correct_points(raw_endpoints, request.camera);
  cv::Mat homography;
  const auto nominal = distance_with(fit_image, fit_board, endpoints[0], endpoints[1], &homography);
  if (!nominal) {
    Result held =
        withheld("markers_degenerate",
                 "The marker correspondences did not produce a usable homography. No dimension is "
                 "reported. The markers may be collinear or the detection may be unstable.");
    held.detected_ids = result.detected_ids;
    held.marker_corners_px = result.marker_corners_px;
    held.fit_ids = result.fit_ids;
    held.check_ids = result.check_ids;
    held.hull_area_mm2 = result.hull_area_mm2;
    held.minor_extent_mm = result.minor_extent_mm;
    copy_identity(held, request);
    return held;
  }
  const Residual fit_residual = residuals_of(homography, fit_image, fit_board);
  const Residual check_residual = residuals_of(homography, check_image, check_board);
  result.rms_residual_mm = round_hundredth(fit_residual.rms);
  result.check_residual_mm = round_hundredth(check_residual.rms);
  if (fit_residual.rms > kMaxFitRmsMm || check_residual.rms > kMaxCheckRmsMm ||
      fit_residual.max > kMaxCornerResidualMm || check_residual.max > kMaxCornerResidualMm) {
    Result held = withheld(
        "calibration_residual",
        "Marker residuals are above the calibration bound (fit RMS " +
            format_fixed(fit_residual.rms, 2) + " mm, check RMS " +
            format_fixed(check_residual.rms, 2) + " mm, limits " + format_fixed(kMaxFitRmsMm, 2) +
            " mm RMS and " + format_fixed(kMaxCornerResidualMm, 2) +
            " mm on a corner). No dimension is reported. Confirm the board version, focus, and "
            "the declared distortion.");
    held.detected_ids = result.detected_ids;
    held.marker_corners_px = result.marker_corners_px;
    held.fit_ids = result.fit_ids;
    held.check_ids = result.check_ids;
    held.hull_area_mm2 = result.hull_area_mm2;
    held.minor_extent_mm = result.minor_extent_mm;
    held.rms_residual_mm = result.rms_residual_mm;
    held.check_residual_mm = result.check_residual_mm;
    copy_identity(held, request);
    return held;
  }

  std::vector<cv::Point2f> fit_board_f;
  for (const auto &point : fit_board) {
    fit_board_f.emplace_back(static_cast<float>(point.x), static_cast<float>(point.y));
  }
  std::vector<cv::Point2f> fit_hull;
  cv::convexHull(fit_board_f, fit_hull);
  std::vector<cv::Point2f> mapped_endpoints;
  cv::perspectiveTransform(endpoints, mapped_endpoints, homography);
  for (std::size_t i = 0; i < mapped_endpoints.size(); ++i) {
    const double signed_distance = cv::pointPolygonTest(fit_hull, mapped_endpoints[i], true);
    if (signed_distance < -kRegionSlackMm) {
      Result held = withheld(
          "point_outside_valid_region",
          "Endpoint '" + request.points[i].id +
              "' maps outside the convex hull of the markers used to fit the plane. MVP does not "
              "extrapolate. No dimension is reported. Move both points inside the marker outline.");
      held.detected_ids = result.detected_ids;
      held.marker_corners_px = result.marker_corners_px;
      held.fit_ids = result.fit_ids;
      held.check_ids = result.check_ids;
      held.hull_area_mm2 = result.hull_area_mm2;
      held.minor_extent_mm = result.minor_extent_mm;
      held.rms_residual_mm = result.rms_residual_mm;
      held.check_residual_mm = result.check_residual_mm;
      held.draw_region = true;
      copy_identity(held, request);
      std::vector<cv::Point2f> region;
      cv::perspectiveTransform(fit_hull, region, homography.inv());
      for (const auto &pixel : region) {
        const cv::Point2f shown = distort_pixel(pixel, request.camera);
        held.valid_region_px.push_back(PixelPoint{shown.x, shown.y});
      }
      return held;
    }
  }

  std::vector<double> samples;
  samples.push_back(*nominal);
  const cv::Point2f delta = endpoints[1] - endpoints[0];
  const float length = std::hypot(delta.x, delta.y);
  if (length < 0.5f) {
    Result bad = rejected("schema_invalid",
                          "The two endpoints are closer than 0.5 pixels. Choose distinct points.");
    copy_identity(bad, request);
    return bad;
  }
  const cv::Point2f axis = delta * (1.0f / length);
  const cv::Point2f perpendicular(-axis.y, axis.x);
  const float click = static_cast<float>(request.click_tolerance_px);
  auto consider = [&](cv::Point2f a, cv::Point2f b, const std::vector<cv::Point2f> &images) {
    if (const auto value = distance_with(images, fit_board, a, b, nullptr)) {
      samples.push_back(*value);
    }
  };
  for (float sign : {-1.f, 1.f}) {
    consider(endpoints[0] - sign * click * axis, endpoints[1] + sign * click * axis, fit_image);
    consider(endpoints[0] + sign * click * perpendicular,
             endpoints[1] + sign * click * perpendicular, fit_image);
  }
  for (cv::Point2f shift : {cv::Point2f(click, 0), cv::Point2f(-click, 0), cv::Point2f(0, click),
                            cv::Point2f(0, -click)}) {
    consider(endpoints[0] + shift, endpoints[1], fit_image);
    consider(endpoints[0], endpoints[1] + shift, fit_image);
  }
  cv::Point2f centroid(0, 0);
  for (const auto &point : fit_image) {
    centroid += point;
  }
  centroid *= 1.0f / static_cast<float>(fit_image.size());
  double mean_radius = 0;
  for (const auto &point : fit_image) {
    mean_radius += cv::norm(point - centroid);
  }
  mean_radius /= static_cast<double>(fit_image.size());
  if (mean_radius > 1.0) {
    const float fraction = static_cast<float>(request.marker_corner_tolerance_px / mean_radius);
    for (float sign : {-1.f, 1.f}) {
      std::vector<cv::Point2f> shifted = fit_image;
      for (auto &point : shifted) {
        point = centroid + (point - centroid) * (1.0f + sign * fraction);
      }
      consider(endpoints[0], endpoints[1], shifted);
    }
  }
  double low = *std::min_element(samples.begin(), samples.end()) - request.scale_tolerance_mm;
  double high = *std::max_element(samples.begin(), samples.end()) + request.scale_tolerance_mm;
  if (!std::isfinite(low) || !std::isfinite(high) || high < low) {
    Result held = withheld("measurement_undefined",
                           "The sensitivity samples were not finite. No dimension is reported.");
    copy_identity(held, request);
    return held;
  }
  const double reported = round_nearest_tenth(*nominal);
  const double reported_low = round_outward_low(low);
  const double reported_high = round_outward_high(high);
  result.status = Status::Measured;
  result.category.clear();
  result.distance_mm = reported;
  result.sensitivity_mm = Interval{reported_low, reported_high};
  result.endpoints_mm = std::vector<BoardPoint>{
      BoardPoint{request.points[0].id, round_hundredth(mapped_endpoints[0].x),
                 round_hundredth(mapped_endpoints[0].y)},
      BoardPoint{request.points[1].id, round_hundredth(mapped_endpoints[1].x),
                 round_hundredth(mapped_endpoints[1].y)}};
  result.label = format_fixed(reported, 1) + " mm [" + format_fixed(reported_low, 1) + ", " +
                 format_fixed(reported_high, 1) + "] mm";
  result.message = "Measured " + result.label +
                   ". The interval varies the declared click, marker-corner, and scale bounds. It "
                   "is conditional on the coplanarity attestation and the declared distortion "
                   "model. It is not a certified tolerance.";
  result.draw_span = true;
  result.draw_region = true;
  std::vector<cv::Point2f> region;
  cv::perspectiveTransform(fit_hull, region, homography.inv());
  for (const auto &pixel : region) {
    const cv::Point2f shown = distort_pixel(pixel, request.camera);
    result.valid_region_px.push_back(PixelPoint{shown.x, shown.y});
  }
  return result;
}

json interval_json(const std::optional<Interval> &interval) {
  if (!interval) {
    return nullptr;
  }
  return json{{"low", interval->low}, {"high", interval->high}};
}

} // namespace

Result check_request(const std::filesystem::path &request_path) {
  try {
    Loaded loaded = load_all(request_path);
    if (!loaded.ok) {
      return loaded.error;
    }
    Result result;
    result.status = Status::Measured;
    result.category.clear();
    result.message = "Fixture accepted. Decoded " + std::to_string(loaded.value.image.cols) + "x" +
                     std::to_string(loaded.value.image.rows) + " and the request matches board " +
                     loaded.value.board.board_id + ".";
    result.label = "accepted";
    copy_identity(result, loaded.value.request);
    result.assumptions = "check does not report a dimension.";
    result.uncertainty_method = "not_applicable";
    return result;
  } catch (const cv::Exception &error) {
    Result result = rejected("internal", std::string("OpenCV error during check: ") + error.what());
    result.status = Status::Internal;
    return result;
  } catch (const std::exception &error) {
    Result result = rejected("internal", error.what());
    result.status = Status::Internal;
    return result;
  }
}

Result measure_request(const std::filesystem::path &request_path) {
  try {
    Loaded loaded = load_all(request_path);
    if (!loaded.ok) {
      return loaded.error;
    }
    return measure_loaded(loaded.value.request, loaded.value.board, loaded.value.image);
  } catch (const cv::Exception &error) {
    Result result =
        rejected("internal", std::string("OpenCV error during measurement: ") + error.what());
    result.status = Status::Internal;
    return result;
  } catch (const std::exception &error) {
    Result result = rejected("internal", error.what());
    result.status = Status::Internal;
    return result;
  }
}

Result measure_request_overridden(const std::filesystem::path &request_path,
                                  const std::vector<RequestPoint> &points, bool coplanar) {
  try {
    Loaded loaded = load_all(request_path);
    if (!loaded.ok) {
      return loaded.error;
    }
    if (points.size() != 2) {
      return rejected("schema_invalid", "The viewer needs exactly two points.");
    }
    loaded.value.request.points = points;
    loaded.value.request.coplanar_attestation = coplanar;
    for (const auto &point : points) {
      if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
        Result bad = rejected("nonfinite_point", "A viewer point is not finite.");
        copy_identity(bad, loaded.value.request);
        return bad;
      }
      if (point.x < 0 || point.y < 0 || point.x >= loaded.value.image.cols ||
          point.y >= loaded.value.image.rows) {
        Result bad = rejected("point_outside_image",
                              "A viewer point is outside the photograph. Click inside the image.");
        copy_identity(bad, loaded.value.request);
        return bad;
      }
    }
    return measure_loaded(loaded.value.request, loaded.value.board, loaded.value.image);
  } catch (const cv::Exception &error) {
    Result result =
        rejected("internal", std::string("OpenCV error during measurement: ") + error.what());
    result.status = Status::Internal;
    return result;
  } catch (const std::exception &error) {
    Result result = rejected("internal", error.what());
    result.status = Status::Internal;
    return result;
  }
}

std::string result_to_json(const Result &result) {
  json endpoints = nullptr;
  if (result.endpoints_mm) {
    endpoints = json::array();
    for (const auto &point : *result.endpoints_mm) {
      endpoints.push_back(json{{"id", point.id}, {"x", point.x}, {"y", point.y}});
    }
  }
  json body = {
      {"assumptions", result.assumptions},
      {"board_id", result.board_id},
      {"board_sha256", result.board_sha256},
      {"calibration",
       json{
           {"check_ids", result.check_ids},
           {"check_residual_mm",
            result.check_residual_mm ? json(*result.check_residual_mm) : json(nullptr)},
           {"detected_ids", result.detected_ids},
           {"fit_ids", result.fit_ids},
           {"hull_area_mm2", result.hull_area_mm2 ? json(*result.hull_area_mm2) : json(nullptr)},
           {"minor_extent_mm",
            result.minor_extent_mm ? json(*result.minor_extent_mm) : json(nullptr)},
           {"rms_residual_mm",
            result.rms_residual_mm ? json(*result.rms_residual_mm) : json(nullptr)},
           {"valid_region",
            "convex hull of the marker corners used to fit the homography, in board millimetres"}}},
      {"category", result.category.empty() ? json(nullptr) : json(result.category)},
      {"coplanar_attestation", result.coplanar_attestation},
      {"distance_mm", result.distance_mm ? json(*result.distance_mm) : json(nullptr)},
      {"endpoints_mm", endpoints},
      {"image", result.image},
      {"image_sha256", result.image_sha256},
      {"label", result.label},
      {"message", result.message},
      {"request_sha256", result.request_sha256},
      {"rounding",
       json{{"decimals", kDistanceDecimals}, {"distance", "nearest"}, {"interval", "outward"}}},
      {"schema", "truemeasure.result"},
      {"schema_version", kSchemaVersion},
      {"sensitivity_interval_mm", interval_json(result.sensitivity_mm)},
      {"status", status_name(result.status == Status::Measured && result.label == "accepted"
                                 ? Status::Measured
                                 : result.status)},
      {"uncertainty_method", result.uncertainty_method},
      {"unit_symbol", "mm"},
      {"units", "millimetre"},
  };
  if (result.label == "accepted") {
    body["status"] = "accepted";
    body["schema"] = "truemeasure.check";
  }
  return body.dump(2) + "\n";
}

std::string human_summary(const Result &result, const std::string &json_path,
                          const std::string &annotation_path) {
  std::ostringstream out;
  out << "TrueMeasure " << TRUEMEASURE_VERSION << "\n";
  out << "status: " << (result.label == "accepted" ? "accepted" : status_name(result.status))
      << "\n";
  if (!result.category.empty()) {
    out << "category: " << result.category << "\n";
  }
  if (!result.label.empty()) {
    out << "label: " << result.label << "\n";
  }
  out << "units: millimetre\n";
  if (result.rms_residual_mm) {
    out << "fit residual rms: " << format_fixed(*result.rms_residual_mm, 2) << " mm\n";
  }
  if (!(result.status == Status::Rejected && result.board_id.empty())) {
    out << "coplanar attestation: " << (result.coplanar_attestation ? "yes" : "no") << "\n";
  }
  out << "board: " << result.board_id << "\n";
  out << "image: " << result.image << "\n";
  if (!result.image_sha256.empty()) {
    out << "image_sha256: " << result.image_sha256 << "\n";
  }
  out << result.message << "\n";
  if (!json_path.empty()) {
    out << "json: " << json_path << "\n";
  }
  if (!annotation_path.empty()) {
    out << "annotation: " << annotation_path << "\n";
  }
  return out.str();
}

} // namespace truemeasure
