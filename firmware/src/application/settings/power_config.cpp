#include "application/settings/power_config.h"
#include <ArduinoJson.h>
#include "core/config_json_input.h"

namespace adv {
namespace { constexpr const char* kPath = "/config/power.json"; }
PowerConfigResult parsePowerConfig(const std::string& json) {
  PowerConfigResult result{ConfigStatus::kInvalidConfig};
  if (json.size() > kMaxConfigBytes || json.find('\0') != std::string::npos) return result;
  JsonDocument doc;
  ConfigInput input{json};
  if (deserializeJson(doc, input) || !doc.is<JsonObject>()) return result;
  for (int tail = input.read(); tail >= 0; tail = input.read())
    if (tail != ' ' && tail != '\t' && tail != '\r' && tail != '\n') return result;
  for (JsonPair field : doc.as<JsonObject>()) {
    if (std::string(field.key().c_str(), field.key().size()) != "cpuFrequencyMhz") return result;
  }
  const auto value = doc["cpuFrequencyMhz"];
  if (!value.is<uint32_t>() || value.is<bool>()) return result;
  const PowerConfig config{value.as<uint32_t>()};
  if (config.valid()) { result.status = ConfigStatus::kOk; result.config = config; }
  return result;
}
std::string encodePowerConfig(const PowerConfig& config) {
  return "{\"cpuFrequencyMhz\":" + std::to_string(config.cpuFrequencyMhz) + "}";
}
PowerConfigResult PowerConfigService::read() {
  std::string json;
  const auto status = store_.read(kPath, json);
  return status == ConfigStatus::kOk ? parsePowerConfig(json) : PowerConfigResult{status};
}
PowerConfigResult PowerConfigService::load() {
  auto result = read();
  // Startup uses the default on missing/unreadable/invalid input, without writing
  // a replacement file. The read error remains visible even when applying works.
  const auto applied = power_.configure(result.config.cpuFrequencyMhz);
  result.powerStatus = applied.status;
  if (applied.status != PowerStatus::kOk) result.status = ConfigStatus::kApplyFailed;
  return result;
}
PowerConfigResult PowerConfigService::save(const std::string& json) {
  auto desired = parsePowerConfig(json);
  if (desired.status != ConfigStatus::kOk) return desired;
  const auto applied = power_.configure(desired.config.cpuFrequencyMhz);
  desired.powerStatus = applied.status;
  if (applied.status != PowerStatus::kOk) { desired.status = ConfigStatus::kApplyFailed; return desired; }
  const auto previous = read();
  if (previous.status != ConfigStatus::kOk && previous.status != ConfigStatus::kNotFound &&
      previous.status != ConfigStatus::kInvalidConfig) { desired.status = previous.status; return desired; }
  if (previous.status != ConfigStatus::kOk || !(previous.config == desired.config)) {
    desired.status = store_.replace(kPath, encodePowerConfig(desired.config));
    if (desired.status != ConfigStatus::kOk) return desired;
  }
  // Even after a no-op save, verify disk. A failed readback may already have
  // persisted the new value; retry can confirm it without another flash write.
  const auto verified = read();
  if (verified.status != ConfigStatus::kOk || !(verified.config == desired.config))
    desired.status = ConfigStatus::kReloadFailed;
  return desired;
}
}  // namespace adv
