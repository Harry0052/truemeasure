#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace truemeasure {

inline constexpr int kSchemaVersion = 1;
inline constexpr int kMinMarkers = 4;
inline constexpr int kMinMarkersWithCheck = 5;
inline constexpr double kMinHullAreaMm2 = 8000.0;
inline constexpr double kMinMinorExtentMm = 50.0;
inline constexpr double kMaxFitRmsMm = 0.35;
inline constexpr double kMaxCheckRmsMm = 0.35;
inline constexpr double kMaxCornerResidualMm = 0.50;
inline constexpr double kRegionSlackMm = 0.5;
inline constexpr int kDistanceDecimals = 1;
inline constexpr int kMaxEdgePx = 4096;
inline constexpr int kMaxPixels = 4096 * 2160;
inline constexpr std::uintmax_t kMaxImageBytes = 16u * 1024u * 1024u;
inline constexpr std::uintmax_t kMaxJsonBytes = 1024u * 1024u;
inline constexpr double kMaxScaleToleranceMm = 25.0;
inline constexpr double kMaxClickTolerancePx = 40.0;
inline constexpr double kMaxMarkerTolerancePx = 5.0;

enum class Status { Measured, Withheld, Rejected, Internal };

struct PixelPoint {
  double x = 0;
  double y = 0;
};

struct CameraModel {
  std::string model = "none";
  double fx = 0;
  double fy = 0;
  double cx = 0;
  double cy = 0;
  std::vector<double> distortion;
};

struct RequestPoint {
  std::string id;
  double x = 0;
  double y = 0;
};

struct Request {
  int schema_version = 0;
  std::string board_id;
  std::string board_file;
  std::string image;
  bool coplanar_attestation = false;
  std::vector<RequestPoint> points;
  CameraModel camera;
  double scale_tolerance_mm = 0;
  double click_tolerance_px = 0;
  double marker_corner_tolerance_px = 0;
  std::string request_sha256;
  std::string image_sha256;
  std::string board_sha256;
};

struct MarkerSpec {
  int id = 0;
  double x_mm = 0;
  double y_mm = 0;
};

struct Board {
  int schema_version = 0;
  std::string board_id;
  std::string units;
  std::string dictionary;
  double marker_size_mm = 0;
  double width_mm = 0;
  double height_mm = 0;
  std::vector<MarkerSpec> markers;
};

struct Interval {
  double low = 0;
  double high = 0;
};

struct BoardPoint {
  std::string id;
  double x = 0;
  double y = 0;
};

struct Result {
  Status status = Status::Internal;
  std::string category;
  std::string message;
  std::string label;
  std::optional<double> distance_mm;
  std::optional<Interval> sensitivity_mm;
  std::optional<std::vector<BoardPoint>> endpoints_mm;
  std::vector<int> detected_ids;
  std::vector<int> fit_ids;
  std::vector<int> check_ids;
  std::optional<double> rms_residual_mm;
  std::optional<double> check_residual_mm;
  std::optional<double> hull_area_mm2;
  std::optional<double> minor_extent_mm;
  std::string uncertainty_method;
  std::string assumptions;
  std::string board_id;
  std::string image;
  std::string request_sha256;
  std::string image_sha256;
  std::string board_sha256;
  bool coplanar_attestation = false;
  std::vector<std::vector<PixelPoint>> marker_corners_px;
  std::vector<PixelPoint> valid_region_px;
  PixelPoint point_a;
  PixelPoint point_b;
  bool draw_span = false;
  bool draw_region = false;
};

const char *status_name(Status status);
int exit_code_for(Status status);
std::string format_fixed(double value, int decimals);

} // namespace truemeasure
