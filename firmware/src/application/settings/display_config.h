#pragma once
#include <cstdint>
#include "platform/config_file_store.h"

namespace adv {
struct DisplayConfig {
  uint8_t brightnessLevel{3};
  uint32_t autoScreenOffSeconds{600};
  bool valid() const {
    const auto seconds = autoScreenOffSeconds;
    return brightnessLevel >= 1 && brightnessLevel <= 5 &&
           (seconds == 0 || seconds == 60 || seconds == 300 || seconds == 600 || seconds == 1800);
  }
  bool operator==(const DisplayConfig& other) const { return brightnessLevel == other.brightnessLevel && autoScreenOffSeconds == other.autoScreenOffSeconds; }
};
struct DisplayConfigResult {
  ConfigStatus status{ConfigStatus::kOk};
  DisplayConfig config{};
  bool changed{false};
};
DisplayConfigResult parseDisplayConfig(const std::string& json);
std::string encodeDisplayConfig(const DisplayConfig& config);
class DisplayConfigService {
 public:
  explicit DisplayConfigService(ConfigFileStore& store) : store_(store) {}
  DisplayConfigResult read();
  DisplayConfigResult reload();
  DisplayConfigResult save(const std::string& json);
  const DisplayConfig& saved() const { return saved_; }
  ConfigStatus configurationStatus() const { return status_; }
 private:
  ConfigFileStore& store_;
  DisplayConfig saved_{};
  ConfigStatus status_{ConfigStatus::kNotFound};
};
}  // namespace adv
