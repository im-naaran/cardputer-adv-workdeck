#pragma once
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
namespace adv {
class ExecIdGenerator {
 public:
  explicit ExecIdGenerator(uint64_t last = 0) : last_(last) {}
  bool next(std::string& result) {
    // Never recycle an ID, including after a BLE reconnect or counter exhaustion.
    if (last_ == std::numeric_limits<uint64_t>::max()) return false;
    char value[17];
    std::snprintf(value, sizeof(value), "%016llx", static_cast<unsigned long long>(++last_));
    result = value;
    return true;
  }
 private:
  uint64_t last_;
};
}
