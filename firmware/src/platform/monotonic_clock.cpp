#include "platform/monotonic_clock.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <M5Cardputer.h>
#endif

namespace adv {

uint32_t MonotonicClock::nowMs() const {
#ifdef ARDUINO
  return millis();
#else
  return 0;
#endif
}

}  // namespace adv

