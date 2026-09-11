#pragma once
#include <array>
#include <cstdint>
#include <functional>

namespace adv {
enum class ScheduledTaskId { kSystemTimeSync, kCodexUsageRefresh, kDisplayRefresh, kBatterySample };
struct ScheduledTaskConfig {
  ScheduledTaskId id;
  uint32_t intervalMs;
  bool enabled;
  bool runImmediately;
};
struct ScheduledTaskSnapshot { bool enabled; uint32_t intervalMs; };
class ScheduledTaskService {
 public:
  using Callback = std::function<void(uint32_t)>;
  bool registerTask(const ScheduledTaskConfig&, Callback, uint32_t nowMs);
  bool getTask(ScheduledTaskId, ScheduledTaskSnapshot&) const;
  bool setEnabled(ScheduledTaskId, bool, uint32_t);
  bool updateInterval(ScheduledTaskId, uint32_t, uint32_t);
  bool triggerNow(ScheduledTaskId, uint32_t);
  bool rescheduleFromNow(ScheduledTaskId, uint32_t);
  bool cancel(ScheduledTaskId);
  void tick(uint32_t);
 private:
  friend struct ScheduledTaskTestAccess;
  struct Slot {
    bool occupied{false};
    ScheduledTaskConfig config{};
    Callback callback;
    uint32_t since{0};
    uint64_t generation{0};
  };
  Slot* find(ScheduledTaskId);
  const Slot* find(ScheduledTaskId) const;
  void dispatch(Slot&, uint32_t);
  std::array<Slot, 8> slots_{};
  uint64_t generation_{0};
  bool dispatching_{false};
};
}
