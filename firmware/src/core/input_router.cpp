#include "core/input_router.h"

namespace adv {
RoutedInput InputRouter::route(const KeyEvent& event, Module module, bool textEditing) const {
  KeyEvent mapped = event;
  const bool extra = event.ctrl || event.shift || event.opt;
  if (event.fn) {
    // Fn owns the entire combination; only this exact page-local exception escapes it.
    if (!extra && !event.alt && event.key == Key::kEnter && module == Module::kCodex)
      return {InputAction::kCodexToggle, event};
    if (extra) return {};
    if (event.character == ',') mapped.key = Key::kLeft;
    if (event.character == '/') mapped.key = Key::kRight;
    switch (mapped.key) {
      case Key::kDigit1: case Key::kDigit2: case Key::kDigit3: case Key::kDigit4:
      case Key::kLeft: case Key::kRight: return {InputAction::kNavigation, mapped};
      default: return {};
    }
  }
  if (event.alt || event.ctrl || event.opt || (event.shift && !textEditing)) {
    char c = event.character;
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (event.alt && !extra && c >= 'a' && c <= 'z') {
      mapped.character = c;
      return {InputAction::kShortcut, mapped};
    }
    return {};
  }
  // Only the explicit editor context bypasses punctuation navigation. Fn and Alt
  // already owned their combinations above, even while typing credentials.
  if (textEditing) {
    if (!event.shift && event.key == Key::kEnter) return {InputAction::kConfirm, mapped};
    if (!event.shift && (event.key == Key::kTab || event.key == Key::kBackspace))
      return {InputAction::kPageKey, mapped};
    if (event.text >= 32 && event.text <= 126) return {InputAction::kPageKey, mapped};
    return {};
  }
  const auto index = static_cast<size_t>(module);
  if (index < config_.directionMapping.size() && config_.directionMapping[index]) {
    switch (event.character) {
      case ';': mapped.key = Key::kUp; break;
      case ',': mapped.key = Key::kLeft; break;
      case '.': mapped.key = Key::kDown; break;
      case '/': mapped.key = Key::kRight; break;
    }
  }
  switch (mapped.key) {
    case Key::kUp: case Key::kDown: case Key::kLeft: case Key::kRight:
      return {InputAction::kDirection, mapped};
    case Key::kEnter: return {InputAction::kConfirm, mapped};
    default: return {InputAction::kPageKey, mapped};
  }
}
}  // namespace adv
