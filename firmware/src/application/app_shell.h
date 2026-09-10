#pragma once

#include "core/navigation_service.h"
#include "platform/display_adapter.h"

namespace adv {

class AppShell {
 public:
  static constexpr int kTopBarHeight = 24;

  void beginFrame(DisplayAdapter& display, Module current, int batteryLevel) const;
  void renderDisconnected(DisplayAdapter& display) const;
  void renderFeedback(DisplayAdapter& display, const std::string& text) const;
  void endFrame(DisplayAdapter& display) const;

 private:
  void drawModuleIcon(DisplayAdapter& display, int index, int x, bool selected) const;
  void drawBatteryLevel(DisplayAdapter& display, int batteryLevel, bool charging) const;
};

}  // namespace adv
