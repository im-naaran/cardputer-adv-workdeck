#include "application/settings/display_config.h"
#include <ArduinoJson.h>
#include "core/config_json_input.h"

namespace adv {
namespace {
constexpr const char* kPath = "/config/display.json";
}
DisplayConfigResult parseDisplayConfig(const std::string& json) {
  DisplayConfigResult result{ConfigStatus::kInvalidConfig};
  if (json.size() > kMaxConfigBytes) return result;
  JsonDocument doc;
  ConfigInput input{json};
  if (deserializeJson(doc, input) || !doc.is<JsonObject>()) return result;
  for (int tail = input.read(); tail >= 0; tail = input.read()) {
    if (tail != ' ' && tail != '\t' && tail != '\r' && tail != '\n') return result;
  }
  for (JsonPair field : doc.as<JsonObject>()) {
    // Preserve embedded NULs so an unknown field cannot match a valid prefix.
    const std::string key(field.key().c_str(), field.key().size());
    if (key != "brightnessLevel" && key != "autoScreenOffSeconds") return result;
  }
  // Legacy brightness-only files retain the default ten-minute timeout.
  if (!doc["autoScreenOffSeconds"].isUnbound()) {
    const auto seconds = doc["autoScreenOffSeconds"];
    if (!seconds.is<uint32_t>() || seconds.is<bool>()) return result;
    result.config.autoScreenOffSeconds = seconds.as<uint32_t>();
  }
  auto level = doc["brightnessLevel"];
  if (!level.is<uint8_t>() || level.is<bool>()) return result;
  result.config.brightnessLevel = level.as<uint8_t>();
  if (result.config.valid()) result.status = ConfigStatus::kOk;
  return result;
}
std::string encodeDisplayConfig(const DisplayConfig& config) {
  return "{\"brightnessLevel\":" + std::to_string(config.brightnessLevel) +
         ",\"autoScreenOffSeconds\":" + std::to_string(config.autoScreenOffSeconds) + "}";
}

DisplayConfigResult DisplayConfigService::read() {
  std::string json;
  const auto status = store_.read(kPath, json);
  auto result = status == ConfigStatus::kOk ? parseDisplayConfig(json) : DisplayConfigResult{status};
  status_ = result.status;
  return result;
}
DisplayConfigResult DisplayConfigService::reload() {
  auto result = read();
  // Preserve the last verified snapshot on failure; status still reports the failed read.
  if (result.status == ConfigStatus::kOk) {
    result.changed = !(saved_ == result.config);
    saved_ = result.config;
  }
  return result;
}
DisplayConfigResult DisplayConfigService::save(const std::string& json) {
  const auto desired = parseDisplayConfig(json);
  if (desired.status != ConfigStatus::kOk) return desired;
  const auto previous = read();
  if (previous.status != ConfigStatus::kOk && previous.status != ConfigStatus::kNotFound &&
      previous.status != ConfigStatus::kInvalidConfig) return previous;
  if (previous.status != ConfigStatus::kOk || !(previous.config == desired.config)) {
    const auto status = store_.replace(kPath, encodeDisplayConfig(desired.config));
    if (status != ConfigStatus::kOk) return {status};
  }
  // Even a no-op save rereads: a failed earlier reload must be retryable without another write.
  auto result = read();
  if (result.status != ConfigStatus::kOk || !(result.config == desired.config)) {
    return {ConfigStatus::kReloadFailed};
  }
  result.changed = !(saved_ == result.config);
  saved_ = result.config;
  return result;
}
}  // namespace adv
