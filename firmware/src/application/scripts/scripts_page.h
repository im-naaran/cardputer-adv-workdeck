#pragma once
#include "application/scripts/scripts_controller.h"
#include "platform/display_adapter.h"
namespace adv {
struct ScriptRowView {
  std::string title, shortcut;
  bool selected{false};
};
struct ScriptsPageView {
  std::vector<ScriptRowView> rows;
  std::string message, hint, footer;
  size_t scrollOffset{0};
};
class ScriptsPage {
 public:
  ScriptsPageView makeView(DisplayAdapter& display, const ScriptsState& state);
  void render(DisplayAdapter& display, const ScriptsState& state);
  void reset() { scrollOffset_ = 0; }
 private:
  size_t scrollOffset_{0};
};
}  // namespace adv
