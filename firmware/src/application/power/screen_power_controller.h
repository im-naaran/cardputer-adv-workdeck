#pragma once
#include <cstdint>
#include "platform/keyboard_adapter.h"

namespace adv {
class ScreenPowerController {
 public:
  enum class State { kAwake, kOff, kWakeUntilRelease };
  void begin(uint32_t now) { state_ = State::kAwake; lastActivity_ = now; anyDown_ = false; }
  void setTimeout(uint32_t seconds) { timeoutMs_ = seconds * 1000u; }
  bool onKeyboard(const KeyboardActivity& activity, uint32_t now);
  void tick(uint32_t now);
  bool visible() const { return state_ != State::kOff; }
 private:
  State state_{State::kAwake};
  uint32_t lastActivity_{0}, timeoutMs_{600000};
  bool anyDown_{false};
};
}  // namespace adv
