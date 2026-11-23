#include "truemeasure/engine.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <iostream>
#include <sstream>
#include <string>

namespace truemeasure {
namespace {

std::string load_page() {
  const std::filesystem::path candidates[] = {
      std::filesystem::path(TRUEMEASURE_SHARE_DIR) / "viewer.html",
      std::filesystem::path("share/viewer.html"),
  };
  for (const auto &path : candidates) {
    std::ifstream in(path);
    if (!in) {
      continue;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!buffer.str().empty()) {
      return buffer.str();
    }
  }
  return {};
}

std::string http_response(int status, const std::string &reason, const std::string &type,
                          const std::string &body) {
  std::ostringstream out;
  out << "HTTP/1.1 " << status << " " << reason << "\r\n";
  out << "Content-Type: " << type << "\r\n";
  out << "Content-Length: " << body.size() << "\r\n";
  out << "Connection: close\r\n";
  out << "Cache-Control: no-store\r\n\r\n";
  out << body;
  return out.str();
}

bool send_all(int fd, const std::string &bytes) {
  std::size_t sent = 0;
  while (sent < bytes.size()) {
    const ssize_t n = ::send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
    if (n <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(n);
  }
  return true;
}

std::string read_http(int fd) {
  std::string data;
  char buffer[4096];
  while (data.size() < 1024u * 1024u) {
    const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
    if (n <= 0) {
      break;
    }
    data.append(buffer, buffer + n);
    const auto header_end = data.find("\r\n\r\n");
    if (header_end == std::string::npos) {
      continue;
    }
    const auto length_at = data.find("Content-Length:");
    std::size_t length = 0;
    if (length_at != std::string::npos && length_at < header_end) {
      length = static_cast<std::size_t>(std::strtoul(data.c_str() + length_at + 15, nullptr, 10));
    }
    if (length > 65536) {
      return {};
    }
    if (data.size() >= header_end + 4 + length) {
      break;
    }
  }
  return data;
}

void handle_client(int fd, const std::filesystem::path &request_path, const std::string &page,
                   const std::string &image_bytes) {
  const std::string raw = read_http(fd);
  if (raw.empty()) {
    send_all(fd, http_response(400, "Bad Request", "text/plain", "empty request\n"));
    return;
  }
  const auto line_end = raw.find("\r\n");
  const std::string start = raw.substr(0, line_end);
  const auto header_end = raw.find("\r\n\r\n");
  const std::string body = header_end == std::string::npos ? "" : raw.substr(header_end + 4);
  if (start.rfind("GET /favicon.ico ", 0) == 0) {
    send_all(fd, http_response(204, "No Content", "image/x-icon", ""));
    return;
  }
  if (start.rfind("GET / ", 0) == 0 || start.rfind("GET /index.html ", 0) == 0) {
    send_all(fd, http_response(200, "OK", "text/html; charset=utf-8", page));
    return;
  }
  if (start.rfind("GET /sample.png ", 0) == 0) {
    send_all(fd, http_response(200, "OK", "image/png", image_bytes));
    return;
  }
  if (start.rfind("POST /measure ", 0) == 0) {
    try {
      const auto payload = nlohmann::json::parse(body);
      if (!payload.contains("points_px") || !payload["points_px"].is_array() ||
          payload["points_px"].size() != 2) {
        send_all(fd, http_response(400, "Bad Request", "application/json",
                                   "{\"status\":\"rejected\",\"category\":\"schema_invalid\"}\n"));
        return;
      }
      std::vector<RequestPoint> points;
      for (const auto &point : payload["points_px"]) {
        RequestPoint item;
        item.id = point.value("id", "");
        item.x = point.at("x").get<double>();
        item.y = point.at("y").get<double>();
        points.push_back(item);
      }
      const bool coplanar = payload.value("coplanar_attestation", false);
      const Result result = measure_request_overridden(request_path, points, coplanar);
      send_all(fd, http_response(200, "OK", "application/json", result_to_json(result)));
    } catch (const std::exception &error) {
      send_all(fd, http_response(400, "Bad Request", "application/json",
                                 std::string("{\"status\":\"rejected\",\"message\":") +
                                     nlohmann::json(error.what()).dump() + "}\n"));
    }
    return;
  }
  send_all(fd, http_response(404, "Not Found", "text/plain", "not found\n"));
}

} // namespace

int serve_viewer(const std::filesystem::path &request_path, int port) {
  const std::string page = load_page();
  if (page.empty()) {
    std::cerr << "viewer page is missing from share/viewer.html\n";
    return 1;
  }
  const Result probed = check_request(request_path);
  if (probed.status != Status::Measured || probed.label != "accepted") {
    std::cerr << probed.message << "\n";
    return exit_code_for(probed.status);
  }
  std::ifstream request_in(request_path);
  const auto parsed = nlohmann::json::parse(
      std::string(std::istreambuf_iterator<char>(request_in), std::istreambuf_iterator<char>()));
  const auto image_path =
      (request_path.parent_path() / parsed.at("image").get<std::string>()).lexically_normal();
  std::ifstream image_in(image_path, std::ios::binary);
  const std::string image_bytes((std::istreambuf_iterator<char>(image_in)),
                                std::istreambuf_iterator<char>());
  if (image_bytes.empty()) {
    std::cerr << "viewer could not read the sample image\n";
    return 1;
  }

  const int server = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server < 0) {
    std::cerr << "socket failed\n";
    return 1;
  }
  const int enabled = 1;
  ::setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<uint16_t>(port));
  if (::bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
    std::cerr << "bind failed: " << std::strerror(errno) << "\n";
    ::close(server);
    return 1;
  }
  sockaddr_in bound{};
  socklen_t bound_len = sizeof(bound);
  ::getsockname(server, reinterpret_cast<sockaddr *>(&bound), &bound_len);
  if (::listen(server, 8) != 0) {
    std::cerr << "listen failed\n";
    ::close(server);
    return 1;
  }
  const int actual = ntohs(bound.sin_port);
  std::cout << "http://127.0.0.1:" << actual << "/\n" << std::flush;
  while (true) {
    const int client = ::accept(server, nullptr, nullptr);
    if (client < 0) {
      continue;
    }
    handle_client(client, request_path, page, image_bytes);
    ::close(client);
  }
}

} // namespace truemeasure
