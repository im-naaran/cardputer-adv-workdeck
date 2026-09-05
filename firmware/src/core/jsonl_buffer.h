#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace adv {

struct BufferResult {
  std::vector<std::string> lines;
  bool overflowed{false};
  bool timedOut{false};
};

class JsonlBuffer {
 public:
  explicit JsonlBuffer(size_t maxBytes = 4096, uint32_t timeoutMs = 5000)
      : maxBytes_(maxBytes), timeoutMs_(timeoutMs) {}

  BufferResult feed(const uint8_t* data, size_t size, uint32_t nowMs);
  BufferResult tick(uint32_t nowMs);
  void clear();
  size_t size() const { return buffer_.size(); }

 private:
  BufferResult expireIfNeeded(uint32_t nowMs);
  std::string buffer_;
  size_t maxBytes_;
  uint32_t timeoutMs_;
  uint32_t lastByteAtMs_{0};
  bool hasPending_{false};
};

}  // namespace adv

