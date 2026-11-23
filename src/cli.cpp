#include "truemeasure/engine.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void print_help() {
  std::cout
      << "TrueMeasure " << TRUEMEASURE_VERSION
      << "\n"
         "Measure a planar distance from a reference-board photo, or withhold it.\n"
         "\n"
         "Usage:\n"
         "  truemeasure check --request FILE\n"
         "  truemeasure measure --request FILE [--json-out FILE] [--annotate FILE]\n"
         "  truemeasure view --request FILE [--port N]\n"
         "  truemeasure --help\n"
         "  truemeasure --version\n"
         "\n"
         "Stdout is the machine-readable JSON document. The human summary is on stderr.\n"
         "Exit codes: 0 measured or accepted, 2 withheld, 3 rejected input, 1 usage or internal.\n"
         "Paths inside a request are relative to that request file.\n";
}

bool take_value(int &index, int argc, char **argv, std::string &value) {
  if (index + 1 >= argc) {
    return false;
  }
  value = argv[++index];
  return true;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    print_help();
    return 1;
  }
  const std::string command = argv[1];
  if (command == "--help" || command == "-h" || command == "help") {
    print_help();
    return 0;
  }
  if (command == "--version") {
    std::cout << TRUEMEASURE_VERSION << "\n";
    return 0;
  }
  std::string request;
  std::string json_out;
  std::string annotate;
  int port = 8765;
  for (int i = 2; i < argc; ++i) {
    const std::string flag = argv[i];
    if (flag == "--request") {
      if (!take_value(i, argc, argv, request)) {
        std::cerr << "--request needs a path\n";
        return 1;
      }
    } else if (flag == "--json-out") {
      if (!take_value(i, argc, argv, json_out)) {
        std::cerr << "--json-out needs a path\n";
        return 1;
      }
    } else if (flag == "--annotate") {
      if (!take_value(i, argc, argv, annotate)) {
        std::cerr << "--annotate needs a path\n";
        return 1;
      }
    } else if (flag == "--port") {
      std::string text;
      if (!take_value(i, argc, argv, text)) {
        std::cerr << "--port needs a number\n";
        return 1;
      }
      port = std::stoi(text);
    } else {
      std::cerr << "unknown argument: " << flag << "\n";
      return 1;
    }
  }
  if (request.empty()) {
    std::cerr << "A --request file is required.\n";
    return 1;
  }
  const std::filesystem::path request_path(request);
  if (command == "check") {
    const truemeasure::Result result = truemeasure::check_request(request_path);
    const std::string json = truemeasure::result_to_json(result);
    std::cout << json;
    std::cerr << truemeasure::human_summary(result, "", "");
    return truemeasure::exit_code_for(result.status);
  }
  if (command == "view") {
    return truemeasure::serve_viewer(request_path, port);
  }
  if (command != "measure") {
    std::cerr << "unknown command: " << command << "\n";
    print_help();
    return 1;
  }
  const truemeasure::Result result = truemeasure::measure_request(request_path);
  const std::string json = truemeasure::result_to_json(result);
  std::cout << json;
  if (!json_out.empty()) {
    const std::filesystem::path output(json_out);
    if (output.has_parent_path()) {
      std::filesystem::create_directories(output.parent_path());
    }
    std::ofstream out(output);
    if (!out) {
      std::cerr << "cannot write " << json_out << "\n";
      return 1;
    }
    out << json;
  }
  if (!annotate.empty()) {
    const auto image_path = (request_path.parent_path() / result.image).lexically_normal();
    if (!truemeasure::write_annotation(image_path, result, annotate)) {
      std::cerr << "cannot write annotation " << annotate << "\n";
      return 1;
    }
  }
  std::cerr << truemeasure::human_summary(result, json_out, annotate);
  return truemeasure::exit_code_for(result.status);
}
