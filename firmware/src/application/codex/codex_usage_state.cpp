#include "application/codex/codex_usage_state.h"

#include "core/protocol_constants.h"

namespace adv {

void CodexUsageState::onSessionReady() {
  if (status_ == UsageStatus::kDisconnected) status_ = UsageStatus::kReadyEmpty;
}

void CodexUsageState::beginRequest(const std::string& execId) {
  if (inFlight() || status_ == UsageStatus::kDisconnected) return;
  inFlightExecId_ = execId;
  if (!hasData()) status_ = UsageStatus::kLoading;
}

bool CodexUsageState::applyResponse(const Message& message, uint32_t receivedAtMs) {
  // A response from an older request must not complete or overwrite the current one.
  if (message.event != MessageEvent::kResponse || message.actionId != protocol::kCodexUsageAction ||
      message.execId != inFlightExecId_ || inFlightExecId_.empty()) {
    return false;
  }
  if (message.resultCode == "OK") {
    if (message.windows.empty()) return false;
    inFlightExecId_.clear();
    lastErrorCode_.clear();
    receivedAtMs_ = receivedAtMs;
    windows_ = message.windows;
    fetchedAtEpochSeconds_ = message.fetchedAtEpochSeconds;
    status_ = UsageStatus::kFresh;
  } else {
    failRequest(message.resultCode);
  }
  return true;
}

void CodexUsageState::failRequest(const std::string& code) {
  if (!inFlight()) return;
  // Errors release the request but preserve the age of the last successful cache.
  inFlightExecId_.clear();
  lastErrorCode_ = code;
  status_ = hasData() ? UsageStatus::kStale
      : code == "NOT_LOGGED_IN" ? UsageStatus::kNotLoggedIn : UsageStatus::kError;
}

void CodexUsageState::disconnect() {
  // Computer-derived cache is session-scoped and cannot survive a BLE disconnect.
  status_ = UsageStatus::kDisconnected;
  inFlightExecId_.clear();
  windows_.clear();
  lastErrorCode_.clear();
  fetchedAtEpochSeconds_ = 0;
  receivedAtMs_ = 0;
}

}  // namespace adv
