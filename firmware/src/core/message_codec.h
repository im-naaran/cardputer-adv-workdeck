#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace adv {

enum class MessageEvent { kRequest, kResponse };

struct UsageWindow {
  std::string limitId;
  std::string limitName;
  std::string windowKind;
  int usedPercent{0};
  bool hasWindowDuration{false};
  int windowDurationMins{0};
  bool hasResetEpoch{false};
  int64_t resetsAtEpochSeconds{0};
};

enum class ActionType { kScript, kClipboard };
enum class NullableBool { kUnknown, kFalse, kTrue };
const char* actionTypeName(ActionType type);

struct ActionEntry {
  std::string actionId;
  std::string name;
  std::string key;
  std::string effectiveKey;
  ActionType type{ActionType::kScript};
};

struct Message {
  MessageEvent event{MessageEvent::kResponse};
  std::string actionId;
  std::string execId;
  std::string resultCode;
  std::string resultMessage;

  uint64_t offset{0};
  uint64_t total{0};
  bool hasNextOffset{false};
  uint64_t nextOffset{0};
  std::vector<ActionEntry> actions;
  ActionType actionType{ActionType::kScript};
  bool hasActionType{false};
  NullableBool clipboardWritten{NullableBool::kUnknown}, pasteSent{NullableBool::kUnknown};
  std::string reason;
  std::string executedActionId;
  std::string executedName;
  bool hasExitCode{false};
  int32_t exitCode{0};

  int protocolVersion{0};
  std::string computerId;
  std::string computerName;
  std::vector<std::string> capabilities;

  std::vector<ActionType> supportedActionTypes;

  int64_t fetchedAtEpochSeconds{0};
  int64_t epochMilliseconds{0};
  int utcOffsetMinutes{0};
  std::vector<UsageWindow> windows;
};

struct DecodeResult {
  bool ok{false};
  Message message;
  std::string error;
};

class MessageCodec {
 public:
  DecodeResult decode(const std::string& json) const;
  std::string encodeCodexUsageRequest(const std::string& execId) const;
  std::string encodeActionsListRequest(const std::string& execId, ActionType type, uint64_t offset) const;
  std::string encodeActionExecuteRequest(const std::string& execId, const std::string& actionId) const;
  std::string encodeShortcutExecuteRequest(const std::string& execId, char key) const;
  std::string encodeTimeRequest(const std::string& execId) const;
};

}  // namespace adv
