#include "truemeasure/engine.hpp"
#include "truemeasure/sha256.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <sys/wait.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

int g_failed = 0;

void expect(bool condition, const std::string &text) {
  if (!condition) {
    std::cerr << "FAIL " << text << "\n";
    ++g_failed;
  }
}

json read_json(const fs::path &path) {
  std::ifstream in(path);
  return json::parse(in);
}

truemeasure::Result measure(const fs::path &path) {
  return truemeasure::measure_request(path);
}

double interval_width(const truemeasure::Result &result) {
  return result.sensitivity_mm->high - result.sensitivity_mm->low;
}

void expect_measured(const fs::path &path) {
  const auto result = measure(path);
  const std::string json_text = truemeasure::result_to_json(result);
  const auto parsed = json::parse(json_text);
  expect(result.status == truemeasure::Status::Measured, path.string() + " measured");
  expect(result.distance_mm.has_value(), path.string() + " has distance");
  expect(result.sensitivity_mm.has_value(), path.string() + " has interval");
  if (result.distance_mm && result.sensitivity_mm) {
    expect(std::abs(*result.distance_mm - 60.0) <= 1.0, path.string() + " absolute error");
    expect(result.sensitivity_mm->low <= 60.0 && result.sensitivity_mm->high >= 60.0,
           path.string() + " interval contains 60");
    expect(result.label.find(truemeasure::format_fixed(*result.distance_mm, 1)) !=
               std::string::npos,
           path.string() + " label uses rounded distance");
    expect(parsed["units"] == "millimetre", "units");
    expect(parsed["unit_symbol"] == "mm", "symbol");
    expect(parsed["label"] == result.label, "json label matches");
    expect(parsed["distance_mm"].get<double>() == *result.distance_mm, "json distance");
    expect(parsed["message"].get<std::string>().find(result.label) != std::string::npos,
           "message contains label");
  }
}

void expect_withheld(const fs::path &path, const std::string &category) {
  const auto result = measure(path);
  const auto parsed = json::parse(truemeasure::result_to_json(result));
  expect(result.status == truemeasure::Status::Withheld, path.string() + " withheld");
  expect(result.category == category, path.string() + " category " + result.category);
  expect(!result.distance_mm.has_value(), path.string() + " has no distance");
  expect(parsed["distance_mm"].is_null(), path.string() + " json distance null");
  expect(parsed["sensitivity_interval_mm"].is_null(), path.string() + " json interval null");
  expect(result.message.find("No dimension") != std::string::npos,
         path.string() + " explains withhold");
}

void expect_rejected(const fs::path &path, const std::string &category) {
  const auto result = measure(path);
  expect(result.status == truemeasure::Status::Rejected, path.string() + " rejected");
  expect(result.category == category, path.string() + " category " + result.category);
  expect(!result.distance_mm.has_value(), path.string() + " rejected without distance");
}

int run_shell(const std::string &command) {
  const int rc = std::system(command.c_str());
  if (rc == -1) {
    return -1;
  }
  return WEXITSTATUS(rc);
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "usage: truemeasure_tests <cli> <source-root>\n";
    return 1;
  }
  const fs::path cli = argv[1];
  const fs::path root = argv[2];
  const fs::path requests = root / "fixtures" / "requests";

  expect(truemeasure::sha256_hex(reinterpret_cast<const std::uint8_t *>("abc"), 3) ==
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
         "sha256 abc");

  const auto started = std::chrono::steady_clock::now();
  expect_measured(requests / "oblique_60mm.json");
  const auto elapsed = std::chrono::steady_clock::now() - started;
  expect(elapsed < std::chrono::seconds(2), "demo measure finishes within 2 seconds");
  expect_measured(requests / "oblique_steep_60mm.json");
  expect_measured(requests / "rotated_60mm.json");
  expect_measured(requests / "lowres_60mm.json");
  expect_measured(requests / "distorted_60mm.json");

  expect_withheld(requests / "top_row_60mm.json", "markers_ill_spread");
  expect_withheld(requests / "cropped_60mm.json", "markers_ill_spread");
  expect_withheld(requests / "two_markers_60mm.json", "markers_insufficient");
  expect_withheld(requests / "blank_60mm.json", "markers_insufficient");
  expect_withheld(requests / "blurred_60mm.json", "markers_insufficient");
  expect_withheld(requests / "outside_board.json", "point_outside_valid_region");
  expect_withheld(requests / "unattested.json", "coplanarity_not_attested");
  expect_withheld(requests / "distorted_undeclared.json", "calibration_residual");
  expect_rejected(requests / "nonfinite.json", "nonfinite_point");
  expect_rejected(requests / "missing_points.json", "schema_invalid");
  expect_rejected(requests / "malformed_image.json", "image_malformed");

  const fs::path temp = fs::temp_directory_path() / "truemeasure-tests";
  fs::create_directories(temp);
  const std::string big((16u * 1024u * 1024u) + 8u, 'x');
  const fs::path oversized = temp / "oversized.png";
  {
    std::ofstream out(oversized, std::ios::binary);
    out << big;
  }
  json oversized_request = read_json(requests / "oblique_60mm.json");
  oversized_request["board_file"] =
      (requests / oversized_request["board_file"].get<std::string>()).lexically_normal().string();
  oversized_request["image"] = oversized.string();
  const fs::path oversized_json = temp / "oversized.json";
  {
    std::ofstream out(oversized_json);
    out << oversized_request.dump(2);
  }
  expect_rejected(oversized_json, "image_oversized");

  json varied = read_json(requests / "oblique_60mm.json");
  varied["board_file"] =
      (requests / varied["board_file"].get<std::string>()).lexically_normal().string();
  varied["image"] = (requests / varied["image"].get<std::string>()).lexically_normal().string();
  varied["endpoint_click_tolerance_px"] = 1.0;
  varied["scale_tolerance_mm"] = 0.3;
  const fs::path click_low = temp / "click_low.json";
  const fs::path click_high = temp / "click_high.json";
  const fs::path scale_high = temp / "scale_high.json";
  {
    std::ofstream out(click_low);
    out << varied.dump(2);
  }
  varied["endpoint_click_tolerance_px"] = 3.0;
  {
    std::ofstream out(click_high);
    out << varied.dump(2);
  }
  varied["endpoint_click_tolerance_px"] = 1.0;
  varied["scale_tolerance_mm"] = 2.0;
  {
    std::ofstream out(scale_high);
    out << varied.dump(2);
  }
  const auto low_click = measure(click_low);
  const auto high_click = measure(click_high);
  const auto high_scale = measure(scale_high);
  expect(low_click.sensitivity_mm && high_click.sensitivity_mm, "click cases measured");
  if (low_click.sensitivity_mm && high_click.sensitivity_mm) {
    expect(interval_width(high_click) > interval_width(low_click) + 0.2,
           "click tolerance expands interval");
  }
  expect(high_scale.sensitivity_mm.has_value(), "scale case measured");
  if (low_click.sensitivity_mm && high_scale.sensitivity_mm) {
    expect(interval_width(high_scale) > interval_width(low_click) + 3.0,
           "scale tolerance expands interval");
  }

  const std::string first = truemeasure::result_to_json(measure(requests / "oblique_60mm.json"));
  const std::string second = truemeasure::result_to_json(measure(requests / "oblique_60mm.json"));
  expect(first == second, "json is reproducible");

  const auto oblique = measure(requests / "oblique_60mm.json");
  const fs::path annotated_a = temp / "oblique_a.png";
  const fs::path annotated_b = temp / "oblique_b.png";
  const auto image_path = (requests / oblique.image).lexically_normal();
  expect(truemeasure::write_annotation(image_path, oblique, annotated_a), "annotation writes");
  expect(truemeasure::write_annotation(image_path, oblique, annotated_b), "annotation rewrites");
  expect(truemeasure::sha256_file(annotated_a.string()) ==
             truemeasure::sha256_file(annotated_b.string()),
         "annotation is deterministic");

  const auto accepted = truemeasure::check_request(requests / "oblique_60mm.json");
  const auto accepted_json = json::parse(truemeasure::result_to_json(accepted));
  expect(accepted_json["status"] == "accepted", "check accepts the valid fixture");
  const auto invalid = truemeasure::check_request(requests / "nonfinite.json");
  expect(invalid.status == truemeasure::Status::Rejected, "check rejects nonfinite");
  expect(invalid.category == "nonfinite_point", "check nonfinite category");

  const fs::path stdout_path = temp / "stdout.json";
  const fs::path stderr_path = temp / "stderr.txt";
  const fs::path json_out = temp / "demo.json";
  const fs::path annotate_out = temp / "demo.png";
  const int measured_rc = run_shell(
      "\"" + cli.string() + "\" measure --request \"" + (requests / "oblique_60mm.json").string() +
      "\" --json-out \"" + json_out.string() + "\" --annotate \"" + annotate_out.string() +
      "\" > \"" + stdout_path.string() + "\" 2> \"" + stderr_path.string() + "\"");
  expect(measured_rc == 0, "cli measure exit 0");
  const auto cli_json = read_json(stdout_path);
  expect(cli_json["status"] == "measured", "cli stdout status");
  expect(cli_json == read_json(json_out), "stdout matches json-out");
  expect(fs::file_size(annotate_out) > 1000, "cli annotation exists");
  const int withheld_rc = run_shell("\"" + cli.string() + "\" measure --request \"" +
                                    (requests / "top_row_60mm.json").string() + "\" > \"" +
                                    (temp / "withheld.json").string() + "\" 2> \"" +
                                    (temp / "withheld.txt").string() + "\"");
  expect(withheld_rc == 2, "cli withhold exit 2");
  const int rejected_rc = run_shell("\"" + cli.string() + "\" measure --request \"" +
                                    (requests / "nonfinite.json").string() + "\" > \"" +
                                    (temp / "rejected.json").string() + "\" 2> \"" +
                                    (temp / "rejected.txt").string() + "\"");
  expect(rejected_rc == 3, "cli reject exit 3");
  const int help_rc =
      run_shell("\"" + cli.string() + "\" --help > \"" + (temp / "help.txt").string() + "\"");
  expect(help_rc == 0, "cli help exit 0");
  std::ifstream help_in(temp / "help.txt");
  const std::string help_text((std::istreambuf_iterator<char>(help_in)),
                              std::istreambuf_iterator<char>());
  expect(help_text.find("Exit codes") != std::string::npos, "help documents exit codes");

  if (g_failed != 0) {
    std::cerr << g_failed << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "all assertions passed\n";
  return 0;
}
