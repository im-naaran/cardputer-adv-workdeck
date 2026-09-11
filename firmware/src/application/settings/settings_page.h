#pragma once
#include <vector>
#include "application/settings/settings_controller.h"
#include "core/input_router.h"
#include "platform/display_adapter.h"

namespace adv {
enum class SettingsScreen { kHome, kBrightness, kCodex, kWifi, kScan, kViewer, kAutoScreenOff };
class SettingsPage {
 public:
  SettingsPage(SettingsController& controller, DisplayAdapter& display)
      : controller_(controller), display_(display) {}
  // Called before the BLE gate by main; Fn/Alt have already been dispatched.
  bool handle(Module module, const RoutedInput& input, uint32_t now = 0);
  bool tick(uint32_t now);
  void leave();
  bool textEditing(Module module) const { return module == Module::kSettings && editing_; }
  void render();
  SettingsScreen screen() const { return screen_; }
  size_t focus() const { return focus_; }
  const std::string& editor() const { return editor_; }
  const std::string& minutes() const { return minutes_; }
  size_t viewPage() const { return viewPage_; }
 private:
  void enter(SettingsScreen screen);
  void edit(size_t field);
  void view();
  void savePending();
  void feedback(const std::string& text);
  void back();
  std::string viewText() const;
  std::vector<std::string> wrap(const std::string& text) const;
  std::vector<std::string> rows() const;

  SettingsController& controller_;
  DisplayAdapter& display_;
  SettingsScreen screen_{SettingsScreen::kHome}, parent_{SettingsScreen::kHome};
  size_t focus_{0}, homeFocus_{0}, returnFocus_{0}, field_{0}, viewPage_{0};
  std::string editor_, minutes_, editMessage_;
  bool minutesDirty_{false}, saveScheduled_{false}, editing_{false};
  uint32_t now_{0}, changedAt_{0}, feedbackAt_{0};
  std::string feedback_;
};
}  // namespace adv
