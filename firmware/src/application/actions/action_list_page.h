#pragma once
#include "application/actions/actions_controller.h"
#include "platform/display_adapter.h"
namespace adv {
struct ActionRowView {
  std::string title, shortcut;
  bool selected{false};
};
struct ActionListPageView {
  std::vector<ActionRowView> rows;
  std::string message, hint, footer;
  size_t scrollOffset{0};
};
class ActionListPage {
 public:
  explicit ActionListPage(ActionType type) : type_(type) {}
  ActionListPageView makeView(DisplayAdapter& display, const ActionDirectoryState& state);
  void render(DisplayAdapter& display, const ActionDirectoryState& state);
  void reset() { scrollOffset_ = 0; }
 private:
  ActionType type_;
  size_t scrollOffset_{0};
};
}  // namespace adv
