#pragma once

#include <cstdint>
#include <vector>

namespace adv {

enum class Key {
  kNone,
  kEnter,
  kUp,
  kDown,
  kLeft,
  kRight,
  kDigit1,
  kDigit2,
  kDigit3,
  kDigit4,
};

struct KeyEvent {
  Key key{Key::kNone};
  bool fn{false};
};

class KeyboardAdapter {
 public:
  void begin();
  void update();
  std::vector<KeyEvent> takePressedEvents();

 private:
  std::vector<KeyEvent> pending_;
};

}  // namespace adv

