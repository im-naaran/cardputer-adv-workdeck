#pragma once
#include <functional>
#include "application/settings/display_config.h"
#include "application/codex/codex_controller.h"
#include "application/wifi/wifi_service.h"

namespace adv {
class SettingsController {
 public:
  SettingsController(DisplayConfigService& display, CodexConfigService& config,
                     CodexController& codex, WifiConfigService& wifiConfig,
                     WifiService& wifi, std::function<void(uint8_t)> brightness)
      : display_(display), config_(config), codex_(codex), wifiConfig_(wifiConfig),
        wifi_(wifi), brightness_(std::move(brightness)) {}
  void loadBrightness();
  void loadWifi();
  void refreshCodex();
  void refreshWifi();
  void setBrightness(int level);
  bool saveMinutes(const std::string& text);
  uint8_t brightnessLevel() const { return level_; }
  uint32_t intervalSeconds() const { return codex_.taskState().intervalMs / 1000; }
  const ConfigResult& savedCodex() const { return savedCodex_; }
  const std::string& displayMessage() const { return displayMessage_; }
  const std::string& codexMessage() const { return codexMessage_; }
  const std::string& wifiMessage() const { return wifiMessage_; }
  const WifiConfig& draft() const { return draft_; }
  void setField(size_t field, const std::string& value);
  bool saveWifi();
  bool scan();
  bool test();
  void retryClose();
  bool tick(uint32_t now);
  const WifiScanResults& scans() const { return scans_; }
  void selectNetwork(size_t index);
  const WifiStatus& operation() const { return observed_; }
  std::string wifiSummary() const;
  std::string wifiDetails() const;
 private:
  bool accept(WifiRequest request);
  bool validateWifi();
  DisplayConfigService& display_;
  CodexConfigService& config_;
  CodexController& codex_;
  WifiConfigService& wifiConfig_;
  WifiService& wifi_;
  std::function<void(uint8_t)> brightness_;
  uint8_t level_{3};
  ConfigResult savedCodex_{};
  WifiConfig draft_{}, saved_{}, tested_{};
  bool savedValid_{false}, haveTest_{false};
  uint64_t requestId_{0};
  WifiStatus observed_{}, lastTest_{};
  WifiAvailability availability_{WifiAvailability::kNotConfigured};
  WifiScanResults scans_{};
  std::string displayMessage_, codexMessage_, wifiMessage_, wifiSaveMessage_;
};
}  // namespace adv
