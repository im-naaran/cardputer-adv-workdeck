#pragma once
#include "platform/config_file_store.h"
#include "platform/power_adapter.h"

namespace adv {
struct PowerConfig {
  uint32_t cpuFrequencyMhz{160};
  bool valid() const { return validCpuFrequency(cpuFrequencyMhz); }
  bool operator==(const PowerConfig& other) const { return cpuFrequencyMhz == other.cpuFrequencyMhz; }
};
struct PowerConfigResult {
  ConfigStatus status{ConfigStatus::kOk};
  PowerConfig config{};
  PowerStatus powerStatus{PowerStatus::kOk};
};
PowerConfigResult parsePowerConfig(const std::string& json);
std::string encodePowerConfig(const PowerConfig& config);
class PowerConfigService {
 public:
  PowerConfigService(ConfigFileStore& store, PowerAdapter& power) : store_(store), power_(power) {}
  PowerConfigResult load();
  PowerConfigResult save(const std::string& json);
  uint32_t frequencyMhz() const { return power_.frequencyMhz(); }
 private:
  PowerConfigResult read();
  ConfigFileStore& store_;
  PowerAdapter& power_;
};
}  // namespace adv
