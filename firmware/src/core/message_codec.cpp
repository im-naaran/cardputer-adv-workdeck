#include "core/message_codec.h"

#include <ArduinoJson.h>
#include <algorithm>

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

bool actionId(const std::string& value, ActionType type) {
  const std::string prefix = std::string(actionTypeName(type)) + ".";
  if (value.size() <= prefix.size() || value.size() > 64 || value.compare(0, prefix.size(), prefix) != 0) return false;
  for (unsigned char c : value) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
  }
  return true;
}
bool readType(JsonVariantConst field, ActionType& type) {
  if (!field.is<const char*>()) return false;
  const auto text = field.as<JsonString>();
  const std::string value(text.c_str(), text.size());
  if (value == "script") type = ActionType::kScript;
  else if (value == "clipboard") type = ActionType::kClipboard;
  else return false;
  return true;
}
bool metadataOnly(JsonObjectConst data) {
  return data["content"].isUnbound() && data["params"].isUnbound() && data["enabled"].isUnbound();
}
// Validate Unicode scalars, including overlong sequences and escaped lone surrogates.
bool validUtf8Name(const std::string& value) {
  bool nonSpace = false;
  for (size_t i = 0; i < value.size();) {
    unsigned char c = value[i++];
    if (c < 128) {
      if (c < 32 || c == 127) return false;
      if (c != 32) nonSpace = true;
      continue;
    }
    unsigned count = c >= 0xC2 && c <= 0xDF ? 1 : c >= 0xE0 && c <= 0xEF ? 2 : c >= 0xF0 && c <= 0xF4 ? 3 : 0;
    if (!count || i + count > value.size()) return false;
    uint32_t scalar = c & ((1u << (6 - count)) - 1);
    for (unsigned j = 0; j < count; ++j) {
      unsigned char next = value[i++];
      if ((next & 0xC0) != 0x80) return false;
      scalar = (scalar << 6) | (next & 0x3F);
    }
    if (scalar < (count == 1 ? 0x80u : count == 2 ? 0x800u : 0x10000u) ||
        scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF)) return false;
    // Match the desktop's Unicode whitespace-only rejection without normalizing names.
    if (!(scalar == 0x85 || scalar == 0xA0 || scalar == 0x1680 ||
          (scalar >= 0x2000 && scalar <= 0x200A) || scalar == 0x2028 || scalar == 0x2029 ||
          scalar == 0x202F || scalar == 0x205F || scalar == 0x3000)) nonSpace = true;
  }
  return nonSpace;
}
bool actionName(const std::string& value) {
  return !value.empty() && value.size() <= 64 && validUtf8Name(value);
}
bool nullableKey(JsonObjectConst object, const char* field, std::string& out) {
  if (object[field].isUnbound()) return false;
  if (object[field].isNull()) return true;
  if (!requiredString(object, field, out)) return false;
  return out.size() == 1 && out[0] >= 'a' && out[0] <= 'z';
}
bool decodePage(JsonObjectConst data, Message& message) {
  if (!readType(data["type"], message.actionType)) return false;
  message.hasActionType = true;
  if (!data["offset"].is<uint64_t>() || !data["total"].is<uint64_t>() ||
      !data["actions"].is<JsonArrayConst>() || data["nextOffset"].isUnbound()) return false;
  message.offset = data["offset"].as<uint64_t>();
  message.total = data["total"].as<uint64_t>();
  if (message.offset % protocol::kActionPageSize ||
      (message.total == 0 ? message.offset != 0 : message.offset >= message.total)) return false;
  auto items = data["actions"].as<JsonArrayConst>();
  const uint64_t remaining = message.total - message.offset;
  const size_t count = remaining < protocol::kActionPageSize ? remaining : protocol::kActionPageSize;
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
    ActionEntry entry;
    if (!metadataOnly(item) || !readType(item["type"], entry.type) || entry.type != message.actionType ||
        !requiredString(item, "actionId", entry.actionId) || !actionId(entry.actionId, entry.type) ||
        !requiredString(item, "name", entry.name) || !actionName(entry.name) ||
        !nullableKey(item, "key", entry.key) || !nullableKey(item, "effectiveKey", entry.effectiveKey) ||
        (!entry.effectiveKey.empty() && entry.effectiveKey != entry.key)) return false;
    for (const auto& previous : message.actions) if (previous.actionId == entry.actionId) return false;
    message.actions.push_back(entry);
  }
  return true;
}
bool nullableBool(JsonVariantConst field, NullableBool& out) {
  if (field.isUnbound()) return false;
  if (field.isNull()) { out = NullableBool::kUnknown; return true; }
  if (!field.is<bool>()) return false;
  out = field.as<bool>() ? NullableBool::kTrue : NullableBool::kFalse;
  return true;
}
bool decodeExecution(JsonVariantConst raw, Message& message) {
  if (raw.isNull() && message.resultCode != "OK") return true;
  if (!raw.is<JsonObjectConst>()) return false;
  auto data = raw.as<JsonObjectConst>();
  if (!metadataOnly(data) || !readType(data["type"], message.actionType) ||
      !requiredString(data, "actionId", message.executedActionId) || !actionId(message.executedActionId, message.actionType) ||
      !requiredString(data, "name", message.executedName) || !actionName(message.executedName) ||
      data["exitCode"].isUnbound() || data["reason"].isUnbound() ||
      !nullableBool(data["clipboardWritten"], message.clipboardWritten) ||
      !nullableBool(data["pasteSent"], message.pasteSent)) return false;
  message.hasActionType = true;
  if (!data["reason"].isNull()) {
    if (!requiredString(data, "reason", message.reason)) return false;
    bool known = false;
    for (const auto* reason : {"UNAVAILABLE", "PERMISSION_DENIED", "WRITE_FAILED", "PASTE_FAILED", "UNCONFIRMED", "EXECUTION_FAILED"})
      if (message.reason == reason) known = true;
    if (!known || message.resultCode == "OK") return false;
  }
  // Null means unconfirmed, never a confirmed false. Each type has its own success condition.
  if (message.actionType == ActionType::kClipboard) {
    if (!data["exitCode"].isNull() ||
        (message.pasteSent == NullableBool::kTrue && message.clipboardWritten != NullableBool::kTrue)) return false;
    return message.resultCode != "OK" || (message.clipboardWritten == NullableBool::kTrue && message.pasteSent == NullableBool::kTrue);
  }
  if (!data["clipboardWritten"].isNull() || !data["pasteSent"].isNull()) return false;
  if (!data["exitCode"].isNull()) {
    if (!data["exitCode"].is<int32_t>()) return false;
    message.hasExitCode = true;
    message.exitCode = data["exitCode"].as<int32_t>();
  }
  return message.resultCode != "OK" || (message.hasExitCode && message.exitCode == 0);
}
bool validActionRequest(const std::string& action, JsonObjectConst payload) {
  ActionType type;
  if (action == protocol::kActionsListAction)
    return payload.size() == 2 && readType(payload["type"], type) && payload["offset"].is<uint64_t>() &&
           payload["offset"].as<uint64_t>() % protocol::kActionPageSize == 0;
  std::string value;
  if (action == protocol::kActionsExecuteAction)
    return payload.size() == 1 && requiredString(payload, "actionId", value) &&
           (actionId(value, ActionType::kScript) || actionId(value, ActionType::kClipboard));
  return payload.size() == 1 && requiredString(payload, "key", value) &&
         value.size() == 1 && value[0] >= 'a' && value[0] <= 'z';
}
std::string actionRequest(const std::string& execId, const char* action,
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

const char* actionTypeName(ActionType type) {
  return type == ActionType::kScript ? "script" : "clipboard";
}

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
    if ((decoded.message.actionId == protocol::kActionsListAction ||
         decoded.message.actionId == protocol::kActionsExecuteAction ||
         decoded.message.actionId == protocol::kShortcutExecuteAction) &&
        !validActionRequest(decoded.message.actionId, root["payload"].as<JsonObjectConst>())) {
      decoded.error = "invalid action request"; return decoded;
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

  if (decoded.message.resultCode != "OK" && decoded.message.resultCode != "ERROR" &&
      decoded.message.resultCode != "BUSY" && decoded.message.resultCode != "TIMEOUT") {
    decoded.error = "invalid result code"; return decoded;
  }
  if (result["data"].isUnbound()) {
    decoded.error = "response data missing";
    return decoded;
  }
  JsonObjectConst data = result["data"].as<JsonObjectConst>();
  if ((decoded.message.actionId == protocol::kActionsListAction && decoded.message.resultCode == "OK" &&
       !decodePage(data, decoded.message)) ||
      ((decoded.message.actionId == protocol::kActionsExecuteAction || decoded.message.actionId == protocol::kShortcutExecuteAction) &&
       !decodeExecution(result["data"], decoded.message))) {
    decoded.error = "invalid action response";
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
    // No v1 fallback: mixed versions cannot safely interpret global action results.
    if (decoded.message.protocolVersion != protocol::kVersion ||
        !data["supportedActionTypes"].is<JsonArrayConst>() || !data["capabilities"].is<JsonArrayConst>()) {
      decoded.error = "incompatible hello"; return decoded;
    }
    for (JsonVariantConst item : data["supportedActionTypes"].as<JsonArrayConst>()) {
      ActionType type;
      auto& types = decoded.message.supportedActionTypes;
      if (!readType(item, type) || std::find(types.begin(), types.end(), type) != types.end()) {
        decoded.error = "invalid supported action types"; return decoded;
      }
      types.push_back(type);
    }
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

std::string MessageCodec::encodeActionsListRequest(const std::string& execId, ActionType type, uint64_t offset) const {
  JsonDocument doc;
  doc["event"] = "request"; doc["actionId"] = protocol::kActionsListAction; doc["execId"] = execId;
  doc["payload"]["type"] = actionTypeName(type); doc["payload"]["offset"] = offset;
  std::string output; serializeJson(doc, output); return output + '\n';
}
std::string MessageCodec::encodeActionExecuteRequest(const std::string& execId, const std::string& actionId) const {
  JsonDocument value;
  value.set(actionId);
  return actionRequest(execId, protocol::kActionsExecuteAction, "actionId", value.as<JsonVariantConst>());
}
std::string MessageCodec::encodeShortcutExecuteRequest(const std::string& execId, char key) const {
  JsonDocument value;
  value.set(std::string(1, key));
  return actionRequest(execId, protocol::kShortcutExecuteAction, "key", value.as<JsonVariantConst>());
}

}  // namespace adv
