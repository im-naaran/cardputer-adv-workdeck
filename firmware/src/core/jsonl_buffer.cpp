#include "core/jsonl_buffer.h"

#include "platform/monotonic_clock.h"

namespace adv {

BufferResult JsonlBuffer::expireIfNeeded(uint32_t nowMs) {
  BufferResult result;
  if (hasPending_ && elapsedMs(nowMs, lastByteAtMs_) >= timeoutMs_) {
    clear();
    result.timedOut = true;
  }
  return result;
}

BufferResult JsonlBuffer::feed(const uint8_t* data, size_t size, uint32_t nowMs) {
  BufferResult result = expireIfNeeded(nowMs);
  for (size_t index = 0; index < size; ++index) {
    const char value = static_cast<char>(data[index]);
    if (value == '\n') {
      if (!buffer_.empty()) result.lines.push_back(buffer_);
      clear();
      continue;
    }
    if (buffer_.size() >= maxBytes_) {
      clear();
      result.overflowed = true;
      // Discard the remainder of this BLE fragment; it belongs to the bad line.
      break;
    }
    buffer_.push_back(value);
    hasPending_ = true;
    lastByteAtMs_ = nowMs;
  }
  return result;
}

BufferResult JsonlBuffer::tick(uint32_t nowMs) { return expireIfNeeded(nowMs); }

void JsonlBuffer::clear() {
  buffer_.clear();
  hasPending_ = false;
  lastByteAtMs_ = 0;
}

}  // namespace adv

