#pragma once

#include <cstdint>
#include <array>
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
  kCharacter,
  kBackspace,
  kTab,
};

struct KeyEvent {
  Key key{Key::kNone};
  bool fn{false};
  char character{0};
  bool alt{false};
  bool ctrl{false};
  bool shift{false};
  bool opt{false};
  char text{0};  // Printable character, independent of the physical shortcut identity.
};

// Base-layer physical identities, independent of Caps Lock and the library's Fn layer.
struct InputSnapshot {
  std::array<bool, 256> pressed{};
  bool fn{false}, alt{false}, ctrl{false}, shift{false}, opt{false};
  std::array<char, 256> shifted{};
};

class KeyPressTracker {
 public:
  std::vector<KeyEvent> update(const InputSnapshot& snapshot);
 private:
  std::array<bool, 256> previous_{};
};

class KeyboardAdapter {
 public:
  void begin();
  void update();
  std::vector<KeyEvent> takePressedEvents();

 private:
  std::vector<KeyEvent> pending_;
  KeyPressTracker tracker_;
};

}  // namespace adv
