#include "core/message_codec.h"

#include <ArduinoJson.h>

#include "core/protocol_constants.h"

namespace adv {
namespace {

bool requiredString(JsonObjectConst object, const char* key, std::string& value) {
  JsonVariantConst field = object[key];
  if (!field.is<const char*>()) return false;
  // Preserve embedded NULs so metadata validation rejects them instead of
  // silently accepting the valid-looking prefix of an ID/name/shortcut.
  const auto text = field.as<JsonString>();
  value.assign(text.c_str(), text.size());
  return !value.empty();
}

bool scriptId(const std::string& value) {
  if (value.size() <= 7 || value.size() > 64 || value.compare(0, 7, "script.") != 0) return false;
  for (unsigned char c : value) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
  }
  return true;
}
bool scriptName(const std::string& value) {
  if (value.empty() || value.size() > 64) return false;
  bool nonSpace = false;
  for (unsigned char c : value) {
    if (c < 32 || c == 127) return false;
    if (c != ' ') nonSpace = true;
  }
  return nonSpace;
}
bool nullableKey(JsonObjectConst object, const char* field, std::string& out) {
  if (object[field].isUnbound()) return false;
  if (object[field].isNull()) return true;
  if (!requiredString(object, field, out)) return false;
  return out.size() == 1 && out[0] >= 'a' && out[0] <= 'z';
}
bool decodePage(JsonObjectConst data, Message& message) {
  if (!data["offset"].is<uint64_t>() || !data["total"].is<uint64_t>() ||
      !data["actions"].is<JsonArrayConst>() || data["nextOffset"].isUnbound()) return false;
  message.offset = data["offset"].as<uint64_t>();
  message.total = data["total"].as<uint64_t>();
  if (message.offset % protocol::kScriptPageSize ||
      (message.total == 0 ? message.offset != 0 : message.offset >= message.total)) return false;
  auto items = data["actions"].as<JsonArrayConst>();
  const uint64_t remaining = message.total - message.offset;
  const size_t count = remaining < protocol::kScriptPageSize ? remaining : protocol::kScriptPageSize;
  if (items.size() != count) return false;
  message.hasNextOffset = remaining > count;
  if (message.hasNextOffset) {
    if (!data["nextOffset"].is<uint64_t>()) return false;
    message.nextOffset = data["nextOffset"].as<uint64_t>();
    if (message.nextOffset != message.offset + count) return false;
  } else if (!data["nextOffset"].isNull()) return false;
  for (JsonVariantConst raw : items) {
    if (!raw.is<JsonObjectConst>()) return false;
    auto item = raw.as<JsonObjectConst>();
    ScriptEntry entry;
    std::string type;
    if (!requiredString(item, "type", type) || type != "script" ||
        !requiredString(item, "actionId", entry.actionId) || !scriptId(entry.actionId) ||
        !requiredString(item, "name", entry.name) || !scriptName(entry.name) ||
        !nullableKey(item, "key", entry.key) || !nullableKey(item, "effectiveKey", entry.effectiveKey) ||
        (!entry.effectiveKey.empty() && entry.effectiveKey != entry.key)) return false;
    for (const auto& previous : message.scripts) if (previous.actionId == entry.actionId) return false;
    message.scripts.push_back(entry);
  }
  return true;
}
bool decodeExecution(JsonVariantConst raw, Message& message) {
  if (raw.isNull() && message.resultCode != "OK") return true;
  if (!raw.is<JsonObjectConst>()) return false;
  auto data = raw.as<JsonObjectConst>();
  if (!requiredString(data, "actionId", message.executedActionId) || !scriptId(message.executedActionId) ||
      !requiredString(data, "name", message.executedName) || !scriptName(message.executedName)) return false;
  if (!data["exitCode"].isNull()) {
    if (!data["exitCode"].is<int32_t>()) return false;
    message.hasExitCode = true;
    message.exitCode = data["exitCode"].as<int32_t>();
  }
  return message.resultCode != "OK" || (message.hasExitCode && message.exitCode == 0);
}
std::string scriptRequest(const std::string& execId, const char* action,
                          const char* field, JsonVariantConst value) {
  JsonDocument doc;
  doc["event"] = "request";
  doc["actionId"] = action;
  doc["execId"] = execId;
  doc["payload"][field] = value;
  std::string output;
  serializeJson(doc, output);
  return output + '\n';
}

}  // namespace

DecodeResult MessageCodec::decode(const std::string& json) const {
  DecodeResult decoded;
  if (json.empty() || json.size() > protocol::kMaxJsonBytes) {
    decoded.error = "message size out of range";
    return decoded;
  }

  JsonDocument document;
  const DeserializationError parseError = deserializeJson(document, json);
  if (parseError) {
    decoded.error = "invalid json";
    return decoded;
  }
  JsonObjectConst root = document.as<JsonObjectConst>();
  std::string event;
  if (!requiredString(root, "event", event) ||
      !requiredString(root, "actionId", decoded.message.actionId) ||
      !requiredString(root, "execId", decoded.message.execId)) {
    decoded.error = "missing envelope field";
    return decoded;
  }
  if (event == "request") {
    decoded.message.event = MessageEvent::kRequest;
    if (!root["payload"].is<JsonObjectConst>()) {
      decoded.error = "request payload missing";
      return decoded;
    }
    decoded.ok = true;
    return decoded;
  }
  if (event != "response") {
    decoded.error = "unsupported event";
    return decoded;
  }

  decoded.message.event = MessageEvent::kResponse;
  JsonObjectConst result = root["result"].as<JsonObjectConst>();
  if (result.isNull() || !requiredString(result, "code", decoded.message.resultCode) ||
      !requiredString(result, "msg", decoded.message.resultMessage)) {
    decoded.error = "response result missing";
    return decoded;
  }

  if (result["data"].isUnbound()) {
    decoded.error = "response data missing";
    return decoded;
  }
  JsonObjectConst data = result["data"].as<JsonObjectConst>();
  if ((decoded.message.actionId == protocol::kActionsListAction && decoded.message.resultCode == "OK" &&
       !decodePage(data, decoded.message)) ||
      ((decoded.message.actionId == protocol::kScriptsExecuteAction || decoded.message.actionId == protocol::kShortcutExecuteAction) &&
       !decodeExecution(result["data"], decoded.message))) {
    decoded.error = "invalid script response";
    return decoded;
  }
  if (decoded.message.actionId == protocol::kHelloAction &&
      decoded.message.resultCode == "OK") {
    if (data.isNull() || !data["protocolVersion"].is<int>() ||
        !requiredString(data, "computerId", decoded.message.computerId) ||
        !requiredString(data, "computerName", decoded.message.computerName)) {
      decoded.error = "invalid hello data";
      return decoded;
    }
    decoded.message.protocolVersion = data["protocolVersion"].as<int>();
    for (JsonVariantConst item : data["capabilities"].as<JsonArrayConst>()) {
      if (item.is<const char*>()) decoded.message.capabilities.emplace_back(item.as<const char*>());
    }
  } else if (decoded.message.actionId == protocol::kTimeReadAction && decoded.message.resultCode == "OK") {
    // Integer-only milliseconds prevent silent truncation and unit ambiguity.
    if (!data["epochMilliseconds"].is<int64_t>() || !data["utcOffsetMinutes"].is<int>() ||
        data["epochMilliseconds"].as<int64_t>() < 0 ||
        data["utcOffsetMinutes"].as<int>() < -720 || data["utcOffsetMinutes"].as<int>() > 840) {
      decoded.error = "invalid time data";
      return decoded;
    }
    decoded.message.epochMilliseconds = data["epochMilliseconds"].as<int64_t>();
    decoded.message.utcOffsetMinutes = data["utcOffsetMinutes"].as<int>();
  } else if (decoded.message.actionId == protocol::kCodexUsageAction &&
             decoded.message.resultCode == "OK") {
    if (data.isNull() || !data["fetchedAtEpochSeconds"].is<int64_t>() ||
        !data["windows"].is<JsonArrayConst>()) {
      decoded.error = "invalid usage data";
      return decoded;
    }
    decoded.message.fetchedAtEpochSeconds = data["fetchedAtEpochSeconds"].as<int64_t>();
    for (JsonObjectConst item : data["windows"].as<JsonArrayConst>()) {
      UsageWindow window;
      if (!requiredString(item, "limitId", window.limitId) ||
          !requiredString(item, "windowKind", window.windowKind) ||
          !item["usedPercent"].is<int>()) {
        decoded.error = "invalid usage window";
        return decoded;
      }
      if (item["limitName"].is<const char*>()) window.limitName = item["limitName"].as<const char*>();
      if (item["windowDurationMins"].is<int>()) {
        window.hasWindowDuration = true;
        window.windowDurationMins = item["windowDurationMins"].as<int>();
      }
      if (item["resetsAtEpochSeconds"].is<int64_t>()) {
        window.hasResetEpoch = true;
        window.resetsAtEpochSeconds = item["resetsAtEpochSeconds"].as<int64_t>();
      }
      window.usedPercent = item["usedPercent"].as<int>();
      decoded.message.windows.push_back(window);
    }
    if (decoded.message.windows.empty()) {
      decoded.error = "usage windows empty";
      return decoded;
    }
  }

  decoded.ok = true;
  return decoded;
}

std::string MessageCodec::encodeCodexUsageRequest(const std::string& execId) const {
  JsonDocument document;
  document["event"] = "request";
  document["actionId"] = protocol::kCodexUsageAction;
  document["execId"] = execId;
  document["payload"].to<JsonObject>();
  std::string output;
  serializeJson(document, output);
  output.push_back('\n');
  return output;
}

std::string MessageCodec::encodeTimeRequest(const std::string& execId) const {
  JsonDocument document;
  document["event"] = "request";
  document["actionId"] = protocol::kTimeReadAction;
  document["execId"] = execId;
  document["payload"].to<JsonObject>();
  std::string output;
  serializeJson(document, output);
  return output + '\n';
}

std::string MessageCodec::encodeActionsListRequest(const std::string& execId, uint64_t offset) const {
  JsonDocument value;
  value.set(offset);
  return scriptRequest(execId, protocol::kActionsListAction, "offset", value.as<JsonVariantConst>());
}
std::string MessageCodec::encodeScriptExecuteRequest(const std::string& execId, const std::string& actionId) const {
  JsonDocument value;
  value.set(actionId);
  return scriptRequest(execId, protocol::kScriptsExecuteAction, "actionId", value.as<JsonVariantConst>());
}
std::string MessageCodec::encodeShortcutExecuteRequest(const std::string& execId, char key) const {
  JsonDocument value;
  value.set(std::string(1, key));
  return scriptRequest(execId, protocol::kShortcutExecuteAction, "key", value.as<JsonVariantConst>());
}

}  // namespace adv
