#include "platform/keyboard_adapter.h"

#ifdef ARDUINO
#include <M5Cardputer.h>
#endif

namespace adv {
std::vector<KeyEvent> KeyPressTracker::update(const InputSnapshot& snapshot) {
  unsigned added = 0;
  uint8_t identity = 0;
  for (size_t i = 0; i < snapshot.pressed.size(); ++i) {
    if (snapshot.pressed[i] && !previous_[i]) { ++added; identity = static_cast<uint8_t>(i); }
  }
  // Remember even ambiguous presses: releasing one of several keys must not trigger another.
  previous_ = snapshot.pressed;
  if (added != 1) return {};
  Key key = Key::kCharacter;
  switch (identity) {
    case '\r': key = Key::kEnter; break;
    case '1': key = Key::kDigit1; break;
    case '2': key = Key::kDigit2; break;
    case '3': key = Key::kDigit3; break;
    case '4': key = Key::kDigit4; break;
  }
  return {{key, snapshot.fn, static_cast<char>(identity), snapshot.alt,
           snapshot.ctrl, snapshot.shift, snapshot.opt}};
}

void KeyboardAdapter::begin() { tracker_ = KeyPressTracker{}; pending_.clear(); }

void KeyboardAdapter::update() {
#ifdef ARDUINO
  M5Cardputer.update();
  InputSnapshot snapshot;
  // isChange only compares key counts. Inspect every scan, including releases and
  // equal-count replacements. The base layer also preserves physical Fn+Enter.
  for (const auto& position : M5Cardputer.Keyboard.keyList()) {
    const uint8_t value = M5Cardputer.Keyboard.getKeyValue(position).value_first;
    switch (value) {
      case KEY_FN: snapshot.fn = true; break;
      case KEY_LEFT_ALT: snapshot.alt = true; break;
      case KEY_LEFT_CTRL: snapshot.ctrl = true; break;
      case KEY_LEFT_SHIFT: snapshot.shift = true; break;
      case KEY_OPT: snapshot.opt = true; break;
      case KEY_ENTER: snapshot.pressed['\r'] = true; break;
      default: snapshot.pressed[value] = true; break;
    }
  }
  const auto events = tracker_.update(snapshot);
  pending_.insert(pending_.end(), events.begin(), events.end());
#endif
}

std::vector<KeyEvent> KeyboardAdapter::takePressedEvents() {
  std::vector<KeyEvent> events;
  events.swap(pending_);
  return events;
}
}  // namespace adv
