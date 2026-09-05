#include "core/message_codec.h"

#include <ArduinoJson.h>

#include "core/protocol_constants.h"

namespace adv {
namespace {

bool requiredString(JsonObjectConst object, const char* key, std::string& value) {
  JsonVariantConst field = object[key];
  if (!field.is<const char*>()) return false;
  value = field.as<const char*>();
  return !value.empty();
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

  JsonObjectConst data = result["data"].as<JsonObjectConst>();
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

}  // namespace adv
