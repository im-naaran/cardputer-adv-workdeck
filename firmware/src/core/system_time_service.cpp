#include "core/system_time_service.h"
namespace adv {
bool SystemTimeService::synchronize(int64_t ms, int offset, uint32_t now) {
  if (ms < 0 || offset < -720 || offset > 840) return false;
  const int64_t seconds = ms / 1000;
  const int64_t local = seconds + static_cast<int64_t>(offset) * 60;
  if (seconds < clock_.minSeconds() || seconds > clock_.maxSeconds() ||
      local < clock_.minSeconds() || local > clock_.maxSeconds()) return false;
  // Commit metadata only after the platform accepts UTC; backward correction is valid.
  if (!clock_.setUtcMilliseconds(ms)) return false;
  hasSynced_ = true;
  lastSuccess_ = now;
  offset_ = offset;
  return true;
}
bool SystemTimeService::currentLocalTime(tm& out) const {
  int64_t ms;
  if (!currentUtcMilliseconds(ms)) return false;
  const int64_t seconds = ms / 1000 + static_cast<int64_t>(offset_) * 60;
  if (seconds < clock_.minSeconds() || seconds > clock_.maxSeconds()) return false;
  const time_t value = static_cast<time_t>(seconds);
  return gmtime_r(&value, &out) != nullptr;
}
}
