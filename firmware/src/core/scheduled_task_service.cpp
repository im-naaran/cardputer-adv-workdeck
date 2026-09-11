#include "core/scheduled_task_service.h"
#include <algorithm>
#include <limits>
#include "platform/monotonic_clock.h"
namespace adv {
namespace {
bool validInterval(uint32_t value) { return value >= 1000 && value <= 0x7fffffffu; }
bool validId(ScheduledTaskId id) {
  return id == ScheduledTaskId::kSystemTimeSync || id == ScheduledTaskId::kCodexUsageRefresh ||
         id == ScheduledTaskId::kDisplayRefresh || id == ScheduledTaskId::kBatterySample;
}
}
ScheduledTaskService::Slot* ScheduledTaskService::find(ScheduledTaskId id) {
  for (auto& s : slots_) if (s.occupied && s.config.id == id) return &s;
  return nullptr;
}
const ScheduledTaskService::Slot* ScheduledTaskService::find(ScheduledTaskId id) const {
  for (const auto& s : slots_) if (s.occupied && s.config.id == id) return &s;
  return nullptr;
}
bool ScheduledTaskService::registerTask(const ScheduledTaskConfig& config, Callback cb, uint32_t now) {
  if (!validId(config.id) || !validInterval(config.intervalMs) || !cb || find(config.id) ||
      (dispatching_ && config.enabled && config.runImmediately) ||
      generation_ == std::numeric_limits<uint64_t>::max()) return false;
  for (auto& s : slots_) if (!s.occupied) {
    s = {true, config, std::move(cb), now, ++generation_};
    if (config.enabled && config.runImmediately) dispatch(s, now);
    return true;
  }
  return false;
}
bool ScheduledTaskService::getTask(ScheduledTaskId id, ScheduledTaskSnapshot& out) const {
  const auto* s = find(id);
  if (!s) return false;
  out = {s->config.enabled, s->config.intervalMs};
  return true;
}
bool ScheduledTaskService::setEnabled(ScheduledTaskId id, bool enabled, uint32_t now) {
  auto* s = find(id);
  if (!s) return false;
  // Repeated hello or UI assignments must not postpone an enabled task.
  if (s->config.enabled != enabled) {
    s->config.enabled = enabled;
    if (enabled) s->since = now;
  }
  return true;
}
bool ScheduledTaskService::updateInterval(ScheduledTaskId id, uint32_t interval, uint32_t now) {
  auto* s = find(id);
  if (!s || !validInterval(interval)) return false;
  s->config.intervalMs = interval;
  s->since = now;
  return true;
}
bool ScheduledTaskService::rescheduleFromNow(ScheduledTaskId id, uint32_t now) {
  auto* s = find(id);
  if (!s) return false;
  s->since = now;
  return true;
}
void ScheduledTaskService::dispatch(Slot& s, uint32_t now) {
  if (s.config.enabled) s.since = now;
  // The callback may cancel/re-register its own slot. Keep its callable alive.
  auto callback = s.callback;
  dispatching_ = true;
  callback(now);
  dispatching_ = false;
}
bool ScheduledTaskService::triggerNow(ScheduledTaskId id, uint32_t now) {
  auto* s = find(id);
  if (!s || dispatching_) return false;
  dispatch(*s, now);
  return true;
}
bool ScheduledTaskService::cancel(ScheduledTaskId id) {
  auto* s = find(id);
  if (!s) return false;
  *s = Slot{};
  return true;
}
std::optional<uint32_t> ScheduledTaskService::nextWaitMs(uint32_t now) const {
  std::optional<uint32_t> nearest;
  for (const auto& s : slots_) {
    if (!s.occupied || !s.config.enabled) continue;
    const uint32_t elapsed = elapsedMs(now, s.since);
    const uint32_t wait = elapsed >= s.config.intervalMs ? 0 : s.config.intervalMs - elapsed;
    if (!nearest || wait < *nearest) nearest = wait;
  }
  return nearest;
}
void ScheduledTaskService::tick(uint32_t now) {
  if (dispatching_) return;
  struct Entry { ScheduledTaskId id; uint64_t generation; };
  std::array<Entry, 8> snapshot{};
  size_t count = 0;
  for (const auto& s : slots_) if (s.occupied) snapshot[count++] = {s.config.id, s.generation};
  std::sort(snapshot.begin(), snapshot.begin() + count,
            [](const Entry& a, const Entry& b) { return a.generation < b.generation; });
  for (size_t i = 0; i < count; ++i) {
    auto* s = find(snapshot[i].id);
    // A replacement occupying the same physical slot is a new task for the next tick.
    if (s && s->generation == snapshot[i].generation && s->config.enabled &&
        elapsedMs(now, s->since) >= s->config.intervalMs) dispatch(*s, now);
  }
}
}
