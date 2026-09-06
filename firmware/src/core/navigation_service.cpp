#include "core/navigation_service.h"

namespace adv {

bool NavigationService::handleGlobal(const KeyEvent& event) {
  if (!event.fn || event.ctrl || event.shift || event.opt) return false;
  int next = static_cast<int>(current_);
  // InputRouter owns physical-key mapping; navigation consumes the mapped key.
  switch (event.key) {
    case Key::kDigit1: next = 0; break;
    case Key::kDigit2: next = 1; break;
    case Key::kDigit3: next = 2; break;
    case Key::kDigit4: next = 3; break;
    case Key::kLeft: next = (next + 3) % 4; break;
    case Key::kRight: next = (next + 1) % 4; break;
    default: return false;
  }
  current_ = static_cast<Module>(next);
  return true;
}

}  // namespace adv
