#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace truemeasure {

std::string sha256_hex(const std::uint8_t *data, std::size_t size);
std::string sha256_file(const std::string &path);

} // namespace truemeasure
