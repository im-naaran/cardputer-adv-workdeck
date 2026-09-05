#pragma once

#include "core/navigation_service.h"
#include "platform/display_adapter.h"

namespace adv {

class PlaceholderPage {
 public:
  void render(DisplayAdapter& display, Module module) const;
};

}  // namespace adv

