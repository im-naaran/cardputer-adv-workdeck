#include "application/codex/codex_config.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>
#include "application/codex/codex_controller.h"

namespace adv {
namespace {
constexpr const char* kPath = "/config/codex.json";

// Track consumption without pulling the standard iostream/locale runtime into firmware.
struct ConfigInput {
  const std::string& bytes;
  size_t offset{0};
  int read() { return offset < bytes.size() ? static_cast<unsigned char>(bytes[offset++]) : -1; }
  size_t readBytes(char* out, size_t count) {
    count = std::min(count, bytes.size() - offset);
    std::memcpy(out, bytes.data() + offset, count);
    offset += count;
    return count;
  }
};
}
ConfigResult parseCodexConfig(const std::string& json) {
  ConfigResult result;
  result.status = ConfigStatus::kInvalidConfig;
  if (json.size() > kMaxConfigBytes) return result;
  JsonDocument document;
  ConfigInput input{json};
  if (deserializeJson(document, input) || !document.is<JsonObject>() || document.size() != 1) return result;
  // ArduinoJson stops at the root's closing brace. Reject a second document or garbage.
  for (int tail = input.read(); tail >= 0; tail = input.read()) {
    if (tail != ' ' && tail != '\t' && tail != '\r' && tail != '\n') return result;
  }
  auto interval = document["refreshIntervalSeconds"];
  if (!interval.is<uint32_t>() || interval.is<bool>()) return result;
  result.config.refreshIntervalSeconds = interval.as<uint32_t>();
  if (result.config.valid()) result.status = ConfigStatus::kOk;
  return result;
}

std::string encodeCodexConfig(const CodexConfig& config) {
  return "{\"refreshIntervalSeconds\":" + std::to_string(config.refreshIntervalSeconds) + "}";
}

ConfigResult CodexConfigService::read() {
  std::string json;
  const auto status = store_.read(kPath, json);
  if (status != ConfigStatus::kOk) return {status};
  return parseCodexConfig(json);
}

ConfigResult CodexConfigService::reload() {
  auto result = read();
  if (result.status != ConfigStatus::kOk) return result;
  const auto before = controller_.taskState().intervalMs;
  // Sample after file I/O; callers also refresh loop time before ticking the scheduler.
  if (!controller_.applyConfig(result.config, now_())) result.status = ConfigStatus::kApplyFailed;
  else result.changed = before != controller_.taskState().intervalMs;
  return result;
}

ConfigResult CodexConfigService::save(const std::string& json) {
  auto desired = parseCodexConfig(json);
  if (desired.status != ConfigStatus::kOk) return desired;
  const auto previous = read();
  if (previous.status != ConfigStatus::kOk && previous.status != ConfigStatus::kNotFound &&
      previous.status != ConfigStatus::kInvalidConfig) return previous;
  if (previous.status != ConfigStatus::kOk ||
      previous.config.refreshIntervalSeconds != desired.config.refreshIntervalSeconds) {
    const auto status = store_.replace(kPath, encodeCodexConfig(desired.config));
    if (status != ConfigStatus::kOk) return {status};
  }
  // Even an unchanged file may not yet be applied. Always reread, never trust the input.
  auto result = reload();
  if (result.status != ConfigStatus::kOk && result.status != ConfigStatus::kApplyFailed) {
    result.status = ConfigStatus::kReloadFailed;
  }
  return result;
}
}  // namespace adv
