#include "application/wifi/wifi_config.h"
#include <ArduinoJson.h>
#include <cstdint>
#include "core/config_json_input.h"

namespace adv {
namespace {
constexpr const char* kPath = "/config/wifi.json";
// Validate Unicode scalars and byte limits before storing credentials. No trimming:
// leading/trailing spaces are part of an SSID or account, not formatting.
bool validText(const std::string& text, size_t maximum) {
  if (text.size() > maximum) return false;
  for (size_t i = 0; i < text.size();) {
    const auto lead = static_cast<unsigned char>(text[i++]);
    uint32_t scalar = lead;
    unsigned continuation = 0;
    uint32_t minimum = 0;
    if (lead >= 0xc2 && lead <= 0xdf) { scalar = lead & 0x1f; continuation = 1; minimum = 0x80; }
    else if (lead >= 0xe0 && lead <= 0xef) { scalar = lead & 0x0f; continuation = 2; minimum = 0x800; }
    else if (lead >= 0xf0 && lead <= 0xf4) { scalar = lead & 7; continuation = 3; minimum = 0x10000; }
    else if (lead >= 0x80) return false;
    if (i + continuation > text.size()) return false;
    while (continuation > 0) {
      --continuation;
      const auto byte = static_cast<unsigned char>(text[i++]);
      if ((byte & 0xc0) != 0x80) return false;
      scalar = (scalar << 6) | (byte & 0x3f);
    }
    if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff) ||
        scalar < 0x20 || (scalar >= 0x7f && scalar <= 0x9f)) return false;
  }
  return true;
}
bool hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
}
bool WifiConfig::valid() const {
  if (ssid.empty() || !validText(ssid, 32) || !validText(username, 64) || !validText(password, 64)) return false;
  // Enterprise passwords need not meet the personal-network 8-character minimum.
  if (!username.empty()) return !password.empty();
  if (password.empty()) return true;
  if (password.size() == 64) {
    for (char c : password) if (!hex(c)) return false;
    return true;
  }
  if (password.size() < 8) return false;
  for (unsigned char c : password) if (c < 32 || c > 126) return false;
  return true;
}
WifiConfigResult parseWifiConfig(const std::string& json) {
  WifiConfigResult result{ConfigStatus::kInvalidConfig};
  if (json.size() > kMaxConfigBytes) return result;
  JsonDocument doc;
  ConfigInput input{json};
  if (deserializeJson(doc, input) || !doc.is<JsonObject>() || doc.size() != 3) return result;
  for (int tail = input.read(); tail >= 0; tail = input.read()) {
    if (tail != ' ' && tail != '\t' && tail != '\r' && tail != '\n') return result;
  }
  std::string* fields[] = {&result.config.ssid, &result.config.username, &result.config.password};
  const char* keys[] = {"ssid", "username", "password"};
  for (size_t i = 0; i < 3; ++i) {
    if (!doc[keys[i]].is<const char*>()) return result;
    const auto value = doc[keys[i]].as<JsonString>();
    // Preserve embedded NUL so validation rejects it instead of accepting a prefix.
    fields[i]->assign(value.c_str(), value.size());
  }
  if (result.config.valid()) result.status = ConfigStatus::kOk;
  return result;
}
std::string encodeWifiConfig(const WifiConfig& config) {
  JsonDocument doc;
  doc["ssid"] = config.ssid; doc["username"] = config.username; doc["password"] = config.password;
  std::string json;
  serializeJson(doc, json);
  return json;
}

WifiConfigResult WifiConfigService::read() {
  std::string json;
  const auto status = store_.read(kPath, json);
  auto result = status == ConfigStatus::kOk ? parseWifiConfig(json) : WifiConfigResult{status};
  status_ = result.status;
  return result;
}
WifiConfigResult WifiConfigService::reload() {
  auto result = read();
  // Preserve the last verified snapshot on failure; status still reports the failed read.
  if (result.status == ConfigStatus::kOk) {
    result.changed = !(saved_ == result.config);
    saved_ = result.config;
  }
  return result;
}
WifiConfigResult WifiConfigService::save(const std::string& json) {
  const auto desired = parseWifiConfig(json);
  if (desired.status != ConfigStatus::kOk) return desired;
  const auto previous = read();
  if (previous.status != ConfigStatus::kOk && previous.status != ConfigStatus::kNotFound &&
      previous.status != ConfigStatus::kInvalidConfig) return previous;
  if (previous.status != ConfigStatus::kOk || !(previous.config == desired.config)) {
    const auto status = store_.replace(kPath, encodeWifiConfig(desired.config));
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
