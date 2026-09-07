#pragma once
#include <cstdint>
#include <string>
#include "application/wifi/wifi_config.h"

namespace adv {
struct WifiNetwork { std::string ssid; int rssi{0}; };
enum class WifiLink { kConnecting, kConnected, kFailed, kDisconnected };
struct WifiConnectionSnapshot {
  uint64_t requestId{0};
  WifiLink link{WifiLink::kConnecting};
  std::string ip;
};
// Platform implementation must invalidate old driver events/IP when starting an
// attempt. The request identity in snapshots belongs to that attempt only.
class WifiAdapter {
 public:
  virtual ~WifiAdapter() = default;
  // Nonblocking start: use RAM credentials, disable auto-reconnect, clear prior
  // enterprise auth before applying this snapshot, and enable STA only on demand.
  virtual bool startConnect(const WifiConfig&, uint64_t requestId) = 0;
  virtual WifiConnectionSnapshot connectionSnapshot() const = 0;
  virtual bool startScan() = 0; // Enable STA for scanning only, never connect saved credentials.
  // -1 means running, -2 means failed, otherwise the number of driver results.
  virtual int pollScan() = 0;
  virtual WifiNetwork scanResult(size_t index) const = 0;
  virtual bool stopScan() = 0;  // Stop activity and free driver results; idempotent.
  virtual bool disconnectAndClearAuth() = 0;
  virtual bool powerOff() = 0;  // True only after Wi-Fi is off; must preserve BLE.
};
}  // namespace adv

namespace adv {
class PlatformWifiAdapter final : public WifiAdapter {
 public:
  bool startConnect(const WifiConfig&, uint64_t requestId) override;
  WifiConnectionSnapshot connectionSnapshot() const override;
  bool startScan() override;
  int pollScan() override;
  WifiNetwork scanResult(size_t index) const override;
  bool stopScan() override;
  bool disconnectAndClearAuth() override;
  bool powerOff() override;
 private:
  bool prepare();
  uint64_t requestId_{0};
  std::string ssid_;
  bool scanActive_{false};
  mutable bool connectedOnce_{false};
};
}  // namespace adv
