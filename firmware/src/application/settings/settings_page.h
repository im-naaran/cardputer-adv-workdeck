#pragma once
#include <vector>
#include "application/settings/settings_controller.h"
#include "core/input_router.h"
#include "platform/display_adapter.h"

namespace adv {
enum class SettingsScreen { kHome, kBrightness, kCodex, kWifi, kScan, kEditor, kViewer };
class SettingsPage {
 public:
  SettingsPage(SettingsController& controller, DisplayAdapter& display)
      : controller_(controller), display_(display) {}
  // Called before the BLE gate by main; Fn/Alt have already been dispatched.
  bool handle(Module module, const RoutedInput& input);
  bool textEditing(Module module) const { return module == Module::kSettings && screen_ == SettingsScreen::kEditor; }
  void render();
  SettingsScreen screen() const { return screen_; }
  size_t focus() const { return focus_; }
  const std::string& editor() const { return editor_; }
  const std::string& minutes() const { return minutes_; }
  size_t viewPage() const { return viewPage_; }
 private:
  void enter(SettingsScreen screen);
  void edit(size_t field);
  void view(size_t field);
  void back();
  std::string viewText() const;
  std::vector<std::string> wrap(const std::string& text) const;
  std::vector<std::string> rows() const;
  void footer(const std::string& text);
  SettingsController& controller_;
  DisplayAdapter& display_;
  SettingsScreen screen_{SettingsScreen::kHome}, parent_{SettingsScreen::kHome};
  size_t focus_{0}, homeFocus_{0}, returnFocus_{0}, field_{0}, viewPage_{0};
  std::string editor_, minutes_, editMessage_;
  bool minutesDirty_{false};
};
}  // namespace adv
