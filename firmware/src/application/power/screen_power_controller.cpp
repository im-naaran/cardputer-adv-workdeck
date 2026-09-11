#include "application/power/screen_power_controller.h"
#include "platform/monotonic_clock.h"

namespace adv {
bool ScreenPowerController::onKeyboard(const KeyboardActivity& activity, uint32_t now) {
  anyDown_ = activity.anyDown;
  if (activity.anyDown || activity.changed || activity.pressedThisUpdate) lastActivity_ = now;
  if (state_ == State::kOff) {
    if (activity.anyDown || activity.pressedThisUpdate) state_ = State::kWakeUntilRelease;
    return false;
  }
  // Consume the whole wake chord and its release, including global shortcuts.
  if (state_ == State::kWakeUntilRelease) {
    if (!activity.anyDown) { state_ = State::kAwake; lastActivity_ = now; }
    return false;
  }
  return true;
}
void ScreenPowerController::tick(uint32_t now) {
  if (state_ == State::kAwake && !anyDown_ && timeoutMs_ &&
      elapsedMs(now, lastActivity_) >= timeoutMs_) state_ = State::kOff;
}
}  // namespace adv
