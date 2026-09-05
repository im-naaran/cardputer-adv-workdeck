#include "platform/keyboard_adapter.h"

#ifdef ARDUINO
#include <M5Cardputer.h>
#endif

namespace adv {

void KeyboardAdapter::begin() {}

void KeyboardAdapter::update() {
#ifdef ARDUINO
  M5Cardputer.update();
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
    return;
  }

  const auto& state = M5Cardputer.Keyboard.keysState();
  // M5Cardputer exposes Fn-layer F1..F4 and arrows directly in KeysState.
  if (state.f1) pending_.push_back({Key::kDigit1, true});
  else if (state.f2) pending_.push_back({Key::kDigit2, true});
  else if (state.f3) pending_.push_back({Key::kDigit3, true});
  else if (state.f4) pending_.push_back({Key::kDigit4, true});
  else if (state.up) pending_.push_back({Key::kUp, state.fn});
  else if (state.down) pending_.push_back({Key::kDown, state.fn});
  else if (state.left) pending_.push_back({Key::kLeft, state.fn});
  else if (state.right) pending_.push_back({Key::kRight, state.fn});
  else if (state.enter) pending_.push_back({Key::kEnter, state.fn});
#endif
}

std::vector<KeyEvent> KeyboardAdapter::takePressedEvents() {
  std::vector<KeyEvent> events;
  events.swap(pending_);
  return events;
}

}  // namespace adv
