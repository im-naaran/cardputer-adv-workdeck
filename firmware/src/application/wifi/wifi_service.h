#pragma once
#include <array>
#include "application/wifi/wifi_config.h"
#include "platform/wifi_adapter.h"
#include "platform/monotonic_clock.h"

namespace adv {
enum class WifiAvailability { kReady, kNotConfigured, kInvalidConfig, kStorageError, kBusy, kReleaseFailed, kIdsExhausted };
enum class WifiPhase { kOff, kScanning, kConnecting, kConnected, kReleasing, kReleaseFailed };
enum class WifiOperation { kNone, kModule, kTest, kScan };
enum class WifiOutcome { kNone, kSucceeded, kFailed, kTimedOut, kDisconnected, kCancelled };
struct WifiRequest { WifiAvailability availability; uint64_t requestId{0}; };
struct WifiStatus {
  bool expired{true};
  bool releaseRetryPending{false};
  uint64_t requestId{0};
  WifiOperation operation{WifiOperation::kNone};
  WifiPhase phase{WifiPhase::kOff};
  WifiOutcome outcome{WifiOutcome::kNone};
  std::string ssid, ip; // Retained result; only isConnected() indicates live connectivity.
};
struct WifiScanResults {
  std::array<WifiNetwork,32> networks{};
  size_t count{0};
  bool truncated{false};
};
class WifiService {
 public:
  WifiService(WifiConfigService& config, WifiAdapter& adapter, const MonotonicClock& clock)
      : config_(config), adapter_(adapter), clock_(clock) {}
  ConfigStatus configurationStatus() const { return config_.configurationStatus(); }
  WifiAvailability canConnect() const;
  WifiRequest connect();
  WifiRequest test(const WifiConfig& draft);
  WifiRequest scan();
  WifiStatus status(uint64_t requestId) const;
  bool isConnected() const;
  WifiStatus close(uint64_t requestId);
  void tick(uint32_t nowMs);
  const WifiScanResults& scanResults() const { return scans_; }
 private:
  friend struct WifiServiceTestAccess;
  WifiAvailability available() const;
  WifiRequest start(WifiOperation, const WifiConfig&);
  void release();
  void merge(const WifiNetwork&);
  WifiConfigService& config_;
  WifiAdapter& adapter_;
  const MonotonicClock& clock_;
  uint64_t lastId_{0};
  uint32_t since_{0};
  uint64_t releaseRetryRequestId_{0};
  uint32_t lastReleaseAttemptMs_{0};
  uint8_t releaseRetriesRemaining_{0};
  WifiStatus state_{};
  WifiConfig attempt_{};
  WifiScanResults scans_{};
};
}  // namespace adv
