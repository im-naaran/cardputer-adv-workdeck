#pragma once

#include "platform/keyboard_adapter.h"

namespace adv {

enum class Module { kCodex = 0, kScripts = 1, kClipboard = 2, kSettings = 3 };

class NavigationService {
 public:
  Module current() const { return current_; }
  bool handleGlobal(const KeyEvent& event);

 private:
  Module current_{Module::kCodex};
};

}  // namespace adv

