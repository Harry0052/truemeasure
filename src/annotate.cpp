#include "truemeasure/engine.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <filesystem>

namespace truemeasure {
namespace {

cv::Scalar ink(int b, int g, int r) {
  return cv::Scalar(b, g, r);
}

void draw_polyline(cv::Mat &image, const std::vector<PixelPoint> &points, const cv::Scalar &color,
                   bool closed) {
  if (points.size() < 2) {
    return;
  }
  std::vector<cv::Point> pixels;
  pixels.reserve(points.size());
  for (const auto &point : points) {
    pixels.emplace_back(static_cast<int>(std::lround(point.x)),
                        static_cast<int>(std::lround(point.y)));
  }
  const std::vector<std::vector<cv::Point>> contour{pixels};
  cv::polylines(image, contour, closed, color, 2, cv::LINE_AA);
}

} // namespace

bool write_annotation(const std::filesystem::path &image_path, const Result &result,
                      const std::filesystem::path &output_path) {
  cv::Mat image = cv::imread(image_path.string(), cv::IMREAD_COLOR);
  if (image.empty()) {
    return false;
  }
  for (const auto &corners : result.marker_corners_px) {
    draw_polyline(image, corners, ink(40, 140, 40), true);
  }
  if (result.draw_region) {
    draw_polyline(image, result.valid_region_px, ink(30, 120, 220), true);
  }
  if (result.draw_span && result.distance_mm) {
    const cv::Point a(static_cast<int>(std::lround(result.point_a.x)),
                      static_cast<int>(std::lround(result.point_a.y)));
    const cv::Point b(static_cast<int>(std::lround(result.point_b.x)),
                      static_cast<int>(std::lround(result.point_b.y)));
    cv::line(image, a, b, ink(160, 70, 20), 2, cv::LINE_AA);
    cv::circle(image, a, 6, ink(160, 70, 20), -1, cv::LINE_AA);
    cv::circle(image, b, 6, ink(160, 70, 20), -1, cv::LINE_AA);
    const std::string text = result.label;
    const cv::Point origin(std::max(8, (a.x + b.x) / 2 - 80), std::max(28, (a.y + b.y) / 2 - 12));
    cv::putText(image, text, origin, cv::FONT_HERSHEY_SIMPLEX, 0.6, ink(250, 250, 250), 4,
                cv::LINE_AA);
    cv::putText(image, text, origin, cv::FONT_HERSHEY_SIMPLEX, 0.6, ink(20, 20, 20), 1,
                cv::LINE_AA);
  } else {
    const std::string banner =
        result.category.empty() ? "dimension withheld" : result.category + ": dimension withheld";
    cv::rectangle(image, cv::Rect(0, 0, image.cols, 36), ink(230, 230, 230), cv::FILLED);
    cv::putText(image, banner, cv::Point(8, 24), cv::FONT_HERSHEY_SIMPLEX, 0.6, ink(20, 20, 140), 1,
                cv::LINE_AA);
  }
  std::filesystem::create_directories(output_path.parent_path());
  return cv::imwrite(output_path.string(), image);
}

} // namespace truemeasure
