#include "platform/wifi_adapter.h"

#ifdef ARDUINO
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_wpa2.h>
#include <esp_netif.h>
#include <cstring>
#endif

namespace adv {
#ifdef ARDUINO
namespace {
// scanDelete frees results but does not reset an interrupted scan's running bit
// or timer in Arduino 2.0.16. Use the library's protected extension points only
// after esp_wifi_scan_stop, so a subsequent request can start a fresh scan.
struct ScanStateAccess : WiFiClass {
  static void resetStoppedScan() {
    clearStatusBits(WIFI_SCANNING_BIT);
    _scanStarted = 0;
  }
};
}
#endif
bool PlatformWifiAdapter::prepare() {
#ifdef ARDUINO
  // Only explicit scan/connect calls reach here. Keep driver credentials in RAM
  // and let WifiService own timeouts and retries, independently of BLE.
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  if (!WiFi.mode(WIFI_STA)) return false;
  return disconnectAndClearAuth();
#else
  return false;
#endif
}

bool PlatformWifiAdapter::startConnect(const WifiConfig& config, uint64_t requestId) {
  if (!requestId || !config.valid() || scanActive_ || requestId_) return false;
#ifdef ARDUINO
  if (!prepare()) return false;
  // The netif outlives the Wi-Fi driver in this Arduino release. Clear its old
  // lease synchronously so a newly associated AP cannot inherit an old test IP.
  auto* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!netif) return false;
  const auto stopped = esp_netif_dhcpc_stop(netif);
  if (stopped != ESP_OK && stopped != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return false;
  esp_netif_ip_info_t empty{};
  if (esp_netif_set_ip_info(netif, &empty) != ESP_OK) return false;
  const auto started = esp_netif_dhcpc_start(netif);
  if (started != ESP_OK && started != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) return false;
  requestId_ = requestId;
  ssid_ = config.ssid;
  connectedOnce_ = false;
  WiFi.setMinSecurity(config.username.empty() && config.password.empty()
                          ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK);
  // Same no-certificate PEAP path as focus-clock. Username is also identity;
  // never apply the personal-network password length rules to this branch.
  if (!config.username.empty()) {
    WiFi.begin(config.ssid.c_str(), WPA2_AUTH_PEAP, config.username.c_str(),
               config.username.c_str(), config.password.c_str(), nullptr, nullptr, nullptr);
  } else {
    WiFi.begin(config.ssid.c_str(), config.password.empty() ? nullptr : config.password.c_str());
  }
  // begin's status can still reflect an earlier queued driver event. Query the
  // actual association and fresh lease below; WifiService bounds the attempt.
  return true;
#else
  return false;
#endif
}

WifiConnectionSnapshot PlatformWifiAdapter::connectionSnapshot() const {
  WifiConnectionSnapshot result{requestId_};
#ifdef ARDUINO
  if (!requestId_ || WiFi.getMode() != WIFI_STA) {
    result.link = WifiLink::kDisconnected;
    return result;
  }
  wifi_ap_record_t ap{};
  const bool associated = esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
      std::string(reinterpret_cast<const char*>(ap.ssid), strnlen(reinterpret_cast<const char*>(ap.ssid), 32)) == ssid_;
  if (associated) {
    auto* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info{};
    if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr) {
      result.link = WifiLink::kConnected;
      result.ip = IPAddress(info.ip.addr).toString().c_str();
      connectedOnce_ = true;
      return result;
    }
  }
  // Avoid attributing a delayed previous-attempt failure event to this request.
  // Before the first association, unknown failure reasons resolve by timeout.
  if (connectedOnce_) result.link = WifiLink::kDisconnected;
#endif
  return result;
}

bool PlatformWifiAdapter::startScan() {
  if (requestId_ || scanActive_) return false;
#ifdef ARDUINO
  if (!prepare()) return false;
  WiFi.scanDelete();
  const int result = WiFi.scanNetworks(true, false);
  scanActive_ = result == WIFI_SCAN_RUNNING || result >= 0;
  return scanActive_;
#else
  return false;
#endif
}
int PlatformWifiAdapter::pollScan() {
#ifdef ARDUINO
  return scanActive_ ? WiFi.scanComplete() : WIFI_SCAN_FAILED;
#else
  return -2;
#endif
}
WifiNetwork PlatformWifiAdapter::scanResult(size_t index) const {
#ifdef ARDUINO
  // The convenience SSID/RSSI methods take uint8_t and wrap at 256. Read the
  // checked record accessor instead, while the driver result buffer is alive.
  const int count = WiFi.scanComplete();
  if (!scanActive_ || count < 0 || index >= static_cast<size_t>(count)) return {};
  const auto* ap = static_cast<const wifi_ap_record_t*>(WiFi.getScanInfoByIndex(static_cast<int>(index)));
  if (!ap) return {};
  return {std::string(reinterpret_cast<const char*>(ap->ssid),
                      strnlen(reinterpret_cast<const char*>(ap->ssid), 32)), ap->rssi};
#else
  (void)index;
  return {};
#endif
}
bool PlatformWifiAdapter::stopScan() {
#ifdef ARDUINO
  bool stopped = true;
  // scanComplete can report its own timeout while the hardware is still scanning.
  // Stop the driver for every owned scan, not just WIFI_SCAN_RUNNING results.
  if (scanActive_ && WiFi.getMode() != WIFI_OFF)
    stopped = esp_wifi_scan_stop() == ESP_OK;
  if (!stopped) return false;
  WiFi.scanDelete();
  ScanStateAccess::resetStoppedScan();
#endif
  scanActive_ = false;
  return true;
}
bool PlatformWifiAdapter::disconnectAndClearAuth() {
#ifdef ARDUINO
  if (WiFi.getMode() == WIFI_OFF) {
    requestId_ = 0; ssid_.clear(); connectedOnce_ = false;
    return true;
  }
  WiFi.setAutoReconnect(false);
  const auto disconnected = esp_wifi_disconnect();
  const auto disabled = esp_wifi_sta_wpa2_ent_disable();
  esp_wifi_sta_wpa2_ent_clear_identity();
  esp_wifi_sta_wpa2_ent_clear_username();
  esp_wifi_sta_wpa2_ent_clear_password();
  esp_wifi_sta_wpa2_ent_clear_ca_cert();
  esp_wifi_sta_wpa2_ent_clear_cert_key();
  if ((disconnected != ESP_OK && disconnected != ESP_ERR_WIFI_NOT_CONNECT) || disabled != ESP_OK) return false;
#endif
  requestId_ = 0; ssid_.clear(); connectedOnce_ = false;
  return true;
}
bool PlatformWifiAdapter::powerOff() {
#ifdef ARDUINO
  // WIFI_OFF stops/deinitializes Wi-Fi only. No Bluetooth stop or Flash erase.
  if (!WiFi.mode(WIFI_OFF) || WiFi.getMode() != WIFI_OFF) return false;
  WiFi.scanDelete();
#endif
  requestId_ = 0; ssid_.clear(); connectedOnce_ = false; scanActive_ = false;
  return true;
}
}  // namespace adv
