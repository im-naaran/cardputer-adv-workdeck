#pragma once
#include <array>
#include "core/navigation_service.h"

namespace adv {
struct InputConfig {
  std::array<bool, 4> directionMapping{{true, true, true, true}};
  bool operator==(const InputConfig& other) const { return directionMapping == other.directionMapping; }
};
enum class InputAction { kConsumed, kNavigation, kCodexToggle, kShortcut, kDirection, kConfirm, kPageKey };
struct RoutedInput {
  InputAction action{InputAction::kConsumed};
  KeyEvent event{};
};
class InputRouter {
 public:
  RoutedInput route(const KeyEvent& event, Module module) const;
  void applyConfig(const InputConfig& config) { config_ = config; }
  const InputConfig& config() const { return config_; }
 private:
  InputConfig config_;
};
}  // namespace adv
