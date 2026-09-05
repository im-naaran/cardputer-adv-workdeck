#pragma once

#include <cstdint>

namespace adv {

class MonotonicClock {
 public:
  virtual ~MonotonicClock() = default;
  virtual uint32_t nowMs() const;
};

// Unsigned subtraction intentionally preserves elapsed time across millis wrap.
inline uint32_t elapsedMs(uint32_t now, uint32_t since) { return now - since; }

}  // namespace adv

