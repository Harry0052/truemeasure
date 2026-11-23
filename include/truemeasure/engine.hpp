#pragma once

#include "truemeasure/types.hpp"

#include <filesystem>
#include <string>

namespace truemeasure {

Result check_request(const std::filesystem::path &request_path);
Result measure_request(const std::filesystem::path &request_path);
Result measure_request_overridden(const std::filesystem::path &request_path,
                                  const std::vector<RequestPoint> &points, bool coplanar);

std::string result_to_json(const Result &result);
std::string human_summary(const Result &result, const std::string &json_path,
                          const std::string &annotation_path);
bool write_annotation(const std::filesystem::path &image_path, const Result &result,
                      const std::filesystem::path &output_path);

int serve_viewer(const std::filesystem::path &request_path, int port);

} // namespace truemeasure
