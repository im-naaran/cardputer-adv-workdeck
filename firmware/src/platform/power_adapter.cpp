#include "platform/power_adapter.h"
#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace adv {
PowerResult PowerAdapter::configure(uint32_t frequency, bool allowAutomaticSleep) {
  const auto previous = frequencyMhz();
  if (!validCpuFrequency(frequency)) return {PowerStatus::kInvalidFrequency, previous};
  // This backend owns fixed frequency only. Never claim sleep support or mix raw
  // clock changes with a PM-managed SDK; the PM backend is a separate gated task.
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
  return {PowerStatus::kUnsupported, previous};
#else
  if (allowAutomaticSleep || !validCpuFrequency(previous)) return {PowerStatus::kUnsupported, previous};
  if (frequency == previous) return {PowerStatus::kOk, previous};
  const bool applied = applyFrequency(frequency);
  auto actual = frequencyMhz();
  if (applied && actual == frequency) return {PowerStatus::kOk, actual};
  // A failed setter may still have changed hardware. Restore and verify instead
  // of reporting the old or requested frequency from a cached value.
  if (actual != previous) {
    const bool restored = applyFrequency(previous);
    actual = frequencyMhz();
    if (!restored || actual != previous) return {PowerStatus::kRestoreFailed, actual};
  }
  return {PowerStatus::kApplyFailed, actual};
#endif
}
uint32_t PlatformPowerAdapter::frequencyMhz() const {
#ifdef ARDUINO
  return getCpuFrequencyMhz();
#else
  return 0;
#endif
}
bool PlatformPowerAdapter::applyFrequency(uint32_t frequency) {
#if defined(ARDUINO) && !CONFIG_PM_ENABLE
  return setCpuFrequencyMhz(frequency);
#else
  return false;
#endif
}
}  // namespace adv
