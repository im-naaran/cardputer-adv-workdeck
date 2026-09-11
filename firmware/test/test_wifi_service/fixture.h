#pragma once
#include <unity.h>
#include <vector>
#include <functional>
#include "application/wifi/wifi_service.h"
using namespace adv;
namespace adv { struct WifiServiceTestAccess { static void exhaust(WifiService& s) { s.lastId_ = UINT64_MAX; } }; }
struct Clock : MonotonicClock { uint32_t now{0}; uint32_t nowMs() const override { return now; } };
struct Store : ConfigFileStore {
  std::string bytes = encodeWifiConfig({"saved", "account", "p"});
  ConfigStatus error{ConfigStatus::kOk}; int reads{0}, writes{0};
  ConfigStatus read(const std::string&, std::string& out) override { ++reads; out=bytes; return error; }
  ConfigStatus replace(const std::string&, const std::string& value) override { ++writes; bytes=value; return ConfigStatus::kOk; }
};
struct Adapter : WifiAdapter {
  int connects{0}, scans{0}, stops{0}, disconnects{0}, offs{0};
  std::function<void()> afterOff;
  bool startOk{true}, stopOk{true}, disconnectOk{true}, offOk{true};
  WifiConfig credentials;
  WifiConnectionSnapshot snapshot;
  int scanState{-1}; std::vector<WifiNetwork> networks;
  bool startConnect(const WifiConfig& c, uint64_t id) override { ++connects; credentials=c; snapshot={id}; return startOk; }
  WifiConnectionSnapshot connectionSnapshot() const override { return snapshot; }
  bool startScan() override { ++scans; return startOk; }
  int pollScan() override { return scanState; }
  WifiNetwork scanResult(size_t index) const override { return networks.at(index); }
  bool stopScan() override { ++stops; networks.clear(); return stopOk; }
  bool disconnectAndClearAuth() override { ++disconnects; return disconnectOk; }
  bool powerOff() override { ++offs; if (afterOff) afterOff(); return offOk; }
};
struct Fixture {
  Store store; WifiConfigService config{store}; Adapter adapter; Clock clock;
  WifiService service{config,adapter,clock};
  void connected() { adapter.snapshot.link=WifiLink::kConnected; adapter.snapshot.ip="192.168.1.2"; service.tick(clock.now); }
};
