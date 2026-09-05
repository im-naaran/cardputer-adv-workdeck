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

struct Message {
  MessageEvent event{MessageEvent::kResponse};
  std::string actionId;
  std::string execId;
  std::string resultCode;
  std::string resultMessage;

  int protocolVersion{0};
  std::string computerId;
  std::string computerName;
  std::vector<std::string> capabilities;

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
  std::string encodeTimeRequest(const std::string& execId) const;
};

}  // namespace adv
