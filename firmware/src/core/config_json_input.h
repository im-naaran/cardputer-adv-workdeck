#pragma once
#include <algorithm>
#include <cstring>
#include <string>

namespace adv {
// Track consumption without pulling the standard iostream/locale runtime into firmware.
struct ConfigInput {
  const std::string& bytes;
  size_t offset{0};
  int read() { return offset < bytes.size() ? static_cast<unsigned char>(bytes[offset++]) : -1; }
  size_t readBytes(char* out, size_t count) {
    count = std::min(count, bytes.size() - offset);
    std::memcpy(out, bytes.data() + offset, count);
    offset += count;
    return count;
  }
};
}  // namespace adv
