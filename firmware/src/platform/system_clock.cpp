#include "platform/system_clock.h"
#ifdef ARDUINO
#include <sys/time.h>
#endif
namespace adv {
bool PlatformSystemClock::setUtcMilliseconds(int64_t value) {
  if (value < 0 || value / 1000 > maxSeconds()) return false;
#ifdef ARDUINO
  timeval tv{};
  tv.tv_sec = static_cast<time_t>(value / 1000);
  tv.tv_usec = static_cast<long>((value % 1000) * 1000);
  return settimeofday(&tv, nullptr) == 0;
#else
  // Native tests must inject a fake, never alter the developer's system clock.
  return false;
#endif
}
bool PlatformSystemClock::readUtcMilliseconds(int64_t& value) const {
#ifdef ARDUINO
  timeval tv{};
  if (gettimeofday(&tv, nullptr) != 0 || tv.tv_sec < 0 ||
      tv.tv_sec > (INT64_MAX - 999) / 1000) return false;
  value = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
  return true;
#else
  (void)value;
  return false;
#endif
}
}
