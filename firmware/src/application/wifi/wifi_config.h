#pragma once
#include "platform/config_file_store.h"

namespace adv {
struct WifiConfig {
  std::string ssid, username, password;
  bool valid() const;
  bool operator==(const WifiConfig& other) const { return ssid == other.ssid && username == other.username && password == other.password; }
};
struct WifiConfigResult {
  ConfigStatus status{ConfigStatus::kOk};
  WifiConfig config{};
  bool changed{false};
};
WifiConfigResult parseWifiConfig(const std::string& json);
std::string encodeWifiConfig(const WifiConfig& config);
class WifiConfigService {
 public:
  explicit WifiConfigService(ConfigFileStore& store) : store_(store) {}
  WifiConfigResult read();
  WifiConfigResult reload();
  WifiConfigResult save(const std::string& json);
  const WifiConfig& saved() const { return saved_; }
  ConfigStatus configurationStatus() const { return status_; }
 private:
  ConfigFileStore& store_;
  WifiConfig saved_{};
  ConfigStatus status_{ConfigStatus::kNotFound};
};
}  // namespace adv
