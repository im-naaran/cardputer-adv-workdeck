#include "application/wifi/wifi_service.h"
#include <algorithm>
#include <limits>

namespace adv {
namespace {
WifiAvailability configAvailability(ConfigStatus status) {
  switch (status) {
    case ConfigStatus::kOk: return WifiAvailability::kReady;
    case ConfigStatus::kNotFound: return WifiAvailability::kNotConfigured;
    case ConfigStatus::kInvalidConfig: return WifiAvailability::kInvalidConfig;
    default: return WifiAvailability::kStorageError;
  }
}
bool hasIp(const std::string& ip) {
  unsigned components = 0, value = 0, digits = 0, nonzero = 0;
  for (size_t i = 0; i <= ip.size(); ++i) {
    if (i == ip.size() || ip[i] == '.') {
      if (!digits || ++components > 4) return false;
      nonzero |= value;
      value = digits = 0;
    } else {
      if (ip[i] < '0' || ip[i] > '9' || ++digits > 3) return false;
      value = value * 10 + static_cast<unsigned>(ip[i] - '0');
      if (value > 255) return false;
    }
  }
  return components == 4 && nonzero;
}
}
WifiAvailability WifiService::available() const {
  if (state_.phase == WifiPhase::kReleaseFailed) return WifiAvailability::kReleaseFailed;
  if (state_.phase != WifiPhase::kOff) return WifiAvailability::kBusy;
  if (lastId_ == std::numeric_limits<uint64_t>::max()) return WifiAvailability::kIdsExhausted;
  return WifiAvailability::kReady;
}
WifiAvailability WifiService::canConnect() const {
  const auto readiness = available();
  return readiness == WifiAvailability::kReady ? configAvailability(configurationStatus()) : readiness;
}
WifiRequest WifiService::connect() {
  const auto readiness = available();
  if (readiness != WifiAvailability::kReady) return {readiness};
  // Readiness is only a hint. Read the current file before granting a new owner.
  const auto loaded = config_.reload();
  if (loaded.status != ConfigStatus::kOk) return {configAvailability(loaded.status)};
  return start(WifiOperation::kModule, loaded.config);
}
WifiRequest WifiService::test(const WifiConfig& draft) {
  const auto readiness = available();
  if (readiness != WifiAvailability::kReady) return {readiness};
  if (!draft.valid()) return {WifiAvailability::kInvalidConfig};
  return start(WifiOperation::kTest, draft);
}
WifiRequest WifiService::scan() {
  const auto readiness = available();
  if (readiness != WifiAvailability::kReady) return {readiness};
  return start(WifiOperation::kScan, {});
}
WifiRequest WifiService::start(WifiOperation operation, const WifiConfig& config) {
  attempt_ = config; // A later edit/save cannot change credentials of this request.
  state_ = {};
  releaseRetryRequestId_ = 0; releaseRetriesRemaining_ = 0;
  state_.expired = false;
  state_.requestId = ++lastId_;
  state_.operation = operation;
  state_.ssid = config.ssid;
  since_ = clock_.nowMs();
  bool started;
  if (operation == WifiOperation::kScan) {
    scans_ = {};
    state_.phase = WifiPhase::kScanning;
    started = adapter_.startScan();
  } else {
    state_.phase = WifiPhase::kConnecting;
    started = adapter_.startConnect(attempt_, state_.requestId);
  }
  if (!started) { state_.outcome = WifiOutcome::kFailed; release(); }
  return {WifiAvailability::kReady, state_.requestId};
}
WifiStatus WifiService::status(uint64_t id) const {
  if (!id || id != state_.requestId) return {};
  return state_;
}
bool WifiService::isConnected() const {
  if (state_.phase != WifiPhase::kConnected) return false;
  const auto snapshot = adapter_.connectionSnapshot();
  return snapshot.requestId == state_.requestId && snapshot.link == WifiLink::kConnected && hasIp(snapshot.ip);
}
WifiStatus WifiService::close(uint64_t id) {
  if (!id || id != state_.requestId) return {};
  if (state_.phase != WifiPhase::kOff) {
    if (state_.outcome == WifiOutcome::kNone) state_.outcome = WifiOutcome::kCancelled;
    release();
  }
  return state_;
}
void WifiService::release() {
  // A request receives one budget; neither automatic nor manual failures refill it.
  if (releaseRetryRequestId_ != state_.requestId) {
    releaseRetryRequestId_ = state_.requestId;
    releaseRetriesRemaining_ = 3;
  }
  state_.phase = WifiPhase::kReleasing;
  // Attempt all cleanup steps even if one fails. Keep ownership on error so a
  // stale close cannot interrupt a newer request and the owner can retry.
  const bool scanStopped = state_.operation != WifiOperation::kScan || adapter_.stopScan();
  const bool disconnected = adapter_.disconnectAndClearAuth();
  const bool off = adapter_.powerOff();
  state_.phase = scanStopped && disconnected && off ? WifiPhase::kOff : WifiPhase::kReleaseFailed;
  // Measure the quiet interval after hardware calls, which may take time.
  lastReleaseAttemptMs_ = clock_.nowMs();
  state_.releaseRetryPending = state_.phase == WifiPhase::kReleaseFailed && releaseRetriesRemaining_ > 0;
  if (state_.phase == WifiPhase::kOff) {
    attempt_ = {}; releaseRetryRequestId_ = 0; releaseRetriesRemaining_ = 0;
  }
}
void WifiService::merge(const WifiNetwork& candidate) {
  // Reuse credential text validation for SSID byte/UTF-8/control constraints.
  if (!WifiConfig{candidate.ssid, "", ""}.valid()) return;
  size_t index = 0;
  while (index < scans_.count && scans_.networks[index].ssid != candidate.ssid) ++index;
  if (index < scans_.count && scans_.networks[index].rssi >= candidate.rssi) return;
  if (index == scans_.count) {
    if (scans_.count == scans_.networks.size()) {
      scans_.truncated = true;
      index = scans_.count - 1;
      if (scans_.networks[index].rssi >= candidate.rssi) return;
    } else ++scans_.count;
  }
  scans_.networks[index] = candidate;
  std::sort(scans_.networks.begin(), scans_.networks.begin() + scans_.count,
            [](const WifiNetwork& a, const WifiNetwork& b) { return a.rssi > b.rssi; });
}
void WifiService::tick(uint32_t now) {
  if (state_.phase == WifiPhase::kReleaseFailed) {
    if (releaseRetryRequestId_ == state_.requestId && releaseRetriesRemaining_ &&
        elapsedMs(now, lastReleaseAttemptMs_) >= 1000) {
      --releaseRetriesRemaining_;
      release();
    }
    return;
  }
  if (state_.phase == WifiPhase::kScanning) {
    const int count = adapter_.pollScan();
    if (count >= 0) {
      for (int i = 0; i < count; ++i) merge(adapter_.scanResult(i));
      state_.outcome = WifiOutcome::kSucceeded;
      release();
    } else if (count != -1 || elapsedMs(now, since_) >= 15000) {
      state_.outcome = count != -1 ? WifiOutcome::kFailed : WifiOutcome::kTimedOut;
      release();
    }
    return;
  }
  if (state_.phase != WifiPhase::kConnecting && state_.phase != WifiPhase::kConnected) return;
  const auto snapshot = adapter_.connectionSnapshot();
  if (state_.phase == WifiPhase::kConnected && snapshot.requestId != state_.requestId) {
    state_.outcome = WifiOutcome::kDisconnected;
    release();
    return;
  }
  if (snapshot.requestId == state_.requestId) {
    if (snapshot.link == WifiLink::kConnected && hasIp(snapshot.ip)) {
      state_.ip = snapshot.ip;
      state_.phase = WifiPhase::kConnected;
      state_.outcome = WifiOutcome::kSucceeded;
      // A test owns Wi-Fi only until the result is known. A module owns it until close.
      if (state_.operation == WifiOperation::kTest) release();
      return;
    }
    if (state_.phase == WifiPhase::kConnected || snapshot.link == WifiLink::kFailed) {
      state_.outcome = state_.phase == WifiPhase::kConnected ? WifiOutcome::kDisconnected : WifiOutcome::kFailed;
      release();
      return;
    }
  }
  if (state_.phase == WifiPhase::kConnecting && elapsedMs(now, since_) >= 30000) {
    state_.outcome = WifiOutcome::kTimedOut;
    release();
  }
}
}  // namespace adv
