#pragma once
#include "platform/system_clock.h"
namespace adv {
class SystemTimeService {
 public:
  explicit SystemTimeService(SystemClock& clock) : clock_(clock) {}
  bool synchronize(int64_t epochMs, int offsetMinutes, uint32_t nowMs);
  bool currentUtcMilliseconds(int64_t& out) const { return hasSynced_ && clock_.readUtcMilliseconds(out); }
  bool currentLocalTime(tm& out) const;
  bool hasSynced() const { return hasSynced_; }
  uint32_t lastSuccessfulSyncMs() const { return lastSuccess_; }
  int utcOffsetMinutes() const { return offset_; }
 private:
  SystemClock& clock_;
  bool hasSynced_{false};
  uint32_t lastSuccess_{0};
  int offset_{0};
};
}
