#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/message_codec.h"

namespace adv {

enum class UsageStatus {
  kDisconnected,
  kReadyEmpty,
  kLoading,
  kFresh,
  kStale,
  kNotLoggedIn,
  kError,
};

class CodexUsageState {
 public:
  void onSessionReady();
  void beginRequest(const std::string& execId);
  bool applyResponse(const Message& message, uint32_t receivedAtMs);
  void failRequest(const std::string& code);
  void disconnect();

  UsageStatus status() const { return status_; }
  bool hasData() const { return !windows_.empty(); }
  bool inFlight() const { return !inFlightExecId_.empty(); }
  const std::string& inFlightExecId() const { return inFlightExecId_; }
  const std::vector<UsageWindow>& windows() const { return windows_; }
  const std::string& lastErrorCode() const { return lastErrorCode_; }
  int64_t fetchedAtEpochSeconds() const { return fetchedAtEpochSeconds_; }
  uint32_t receivedAtMs() const { return receivedAtMs_; }

 private:
  UsageStatus status_{UsageStatus::kDisconnected};
  std::string inFlightExecId_;
  std::vector<UsageWindow> windows_;
  std::string lastErrorCode_;
  int64_t fetchedAtEpochSeconds_{0};
  uint32_t receivedAtMs_{0};
};

}  // namespace adv
