#include "application/input/input_config.h"
#include <ArduinoJson.h>
#include "core/config_json_input.h"

namespace adv {
namespace {
constexpr const char* kPath = "/config/input.json";
constexpr const char* kModules[] = {"codex", "scripts", "clipboard", "settings"};
}
InputConfigResult parseInputConfig(const std::string& json) {
  InputConfigResult result{ConfigStatus::kInvalidConfig};
  if (json.size() > kMaxConfigBytes) return result;
  JsonDocument document;
  ConfigInput input{json};
  if (deserializeJson(document, input) || !document.is<JsonObject>()) return result;
  for (int tail = input.read(); tail >= 0; tail = input.read()) {
    if (tail != ' ' && tail != '\t' && tail != '\r' && tail != '\n') return result;
  }
  // Omitted modules/fields are defaults, never patches of the active configuration.
  for (JsonPair pair : document.as<JsonObject>()) {
    size_t index = 0;
    while (index < 4 && std::strcmp(pair.key().c_str(), kModules[index]) != 0) ++index;
    if (index == 4 || !pair.value().is<JsonObject>()) return result;
    for (JsonPair field : pair.value().as<JsonObject>()) {
      if (std::strcmp(field.key().c_str(), "directionMapping") != 0 || !field.value().is<bool>()) return result;
      result.config.directionMapping[index] = field.value().as<bool>();
    }
  }
  result.status = ConfigStatus::kOk;
  return result;
}
std::string encodeInputConfig(const InputConfig& config) {
  JsonDocument document;
  for (size_t i = 0; i < 4; ++i) document[kModules[i]]["directionMapping"] = config.directionMapping[i];
  std::string result;
  serializeJson(document, result);
  return result;
}
InputConfigResult InputConfigService::read() {
  std::string json;
  const auto status = store_.read(kPath, json);
  if (status != ConfigStatus::kOk) return {status};
  return parseInputConfig(json);
}
InputConfigResult InputConfigService::reload() {
  auto result = read();
  // Startup keeps router defaults; any later read failure keeps the last valid values.
  if (result.status != ConfigStatus::kOk) return result;
  result.changed = !(router_.config() == result.config);
  router_.applyConfig(result.config);
  return result;
}
InputConfigResult InputConfigService::save(const std::string& json) {
  const auto desired = parseInputConfig(json);
  if (desired.status != ConfigStatus::kOk) return desired;
  const auto previous = read();
  if (previous.status != ConfigStatus::kOk && previous.status != ConfigStatus::kNotFound &&
      previous.status != ConfigStatus::kInvalidConfig) return previous;
  if (previous.status != ConfigStatus::kOk || !(previous.config == desired.config)) {
    const auto status = store_.replace(kPath, encodeInputConfig(desired.config));
    if (status != ConfigStatus::kOk) return {status};
  }
  // Reread even on a no-op save: persisted and active configurations may differ.
  auto result = reload();
  if (result.status != ConfigStatus::kOk) result.status = ConfigStatus::kReloadFailed;
  return result;
}
}  // namespace adv
