#include "fixture.h"
void configuration_and_idle() {
  Fixture f;
  TEST_ASSERT_TRUE(f.service.canConnect()==WifiAvailability::kNotConfigured);
  f.config.reload();
  TEST_ASSERT_TRUE(f.service.canConnect()==WifiAvailability::kReady);
  for(int i=0;i<20;++i) f.service.tick(i*100000);
  TEST_ASSERT_EQUAL(1,f.store.reads); TEST_ASSERT_EQUAL(0,f.adapter.connects); TEST_ASSERT_EQUAL(0,f.adapter.offs);
  for(auto status:{ConfigStatus::kNotFound,ConfigStatus::kInvalidConfig,ConfigStatus::kReadFailed,ConfigStatus::kNotMounted}) {
    f.store.error=status;
    auto r=f.service.connect();
    TEST_ASSERT_EQUAL(0,r.requestId); TEST_ASSERT_TRUE(r.availability!=WifiAvailability::kReady);
    TEST_ASSERT_EQUAL(0,f.adapter.connects);
  }
  f.store.error=ConfigStatus::kOk; f.store.bytes="{}";
  TEST_ASSERT_TRUE(f.service.connect().availability==WifiAvailability::kInvalidConfig);
}
void ownership_and_saved_snapshot() {
  Fixture f; auto first=f.service.connect();
  TEST_ASSERT_TRUE(first.availability==WifiAvailability::kReady);
  TEST_ASSERT_TRUE(f.service.canConnect()==WifiAvailability::kBusy);
  TEST_ASSERT_TRUE(f.service.connect().availability==WifiAvailability::kBusy);
  TEST_ASSERT_TRUE(f.service.scan().availability==WifiAvailability::kBusy);
  TEST_ASSERT_TRUE(f.service.test({"x","",""}).availability==WifiAvailability::kBusy);
  f.config.save(encodeWifiConfig({"new","","12345678"}));
  TEST_ASSERT_EQUAL_STRING("saved",f.adapter.credentials.ssid.c_str());
  f.connected(); TEST_ASSERT_TRUE(f.service.isConnected());
  f.service.tick(90000); TEST_ASSERT_EQUAL(0,f.adapter.offs);
  TEST_ASSERT_TRUE(f.service.close(first.requestId).phase==WifiPhase::kOff);
  TEST_ASSERT_FALSE(f.service.isConnected());
  f.service.close(first.requestId); TEST_ASSERT_EQUAL(1,f.adapter.offs);
  auto second=f.service.connect(); TEST_ASSERT_TRUE(second.requestId>first.requestId);
  TEST_ASSERT_EQUAL_STRING("new",f.adapter.credentials.ssid.c_str());
  TEST_ASSERT_TRUE(f.service.close(first.requestId).expired);
  TEST_ASSERT_TRUE(f.service.close(0).expired); TEST_ASSERT_EQUAL(1,f.adapter.offs);
  TEST_ASSERT_TRUE(f.service.close(second.requestId).outcome==WifiOutcome::kCancelled);
}
void failures_and_release_retry() {
  for(int failure=0;failure<3;++failure) {
    Fixture f; auto r=f.service.connect();
    f.adapter.stopOk=true; f.adapter.disconnectOk=failure!=0; f.adapter.offOk=failure!=1;
    f.adapter.snapshot.link=WifiLink::kFailed;
    if(failure==2) f.adapter.offOk=false;
    f.service.tick(10);
    TEST_ASSERT_TRUE(f.service.status(r.requestId).outcome==WifiOutcome::kFailed);
    TEST_ASSERT_TRUE(f.service.status(r.requestId).phase==WifiPhase::kReleaseFailed);
    TEST_ASSERT_EQUAL(1,f.adapter.offs);
    TEST_ASSERT_TRUE(f.service.connect().availability==WifiAvailability::kReleaseFailed);
    f.service.tick(50000); TEST_ASSERT_EQUAL(1,f.adapter.offs);
    f.adapter.disconnectOk=true; f.adapter.offOk=true;
    auto state=f.service.close(r.requestId);
    TEST_ASSERT_TRUE(state.phase==WifiPhase::kOff); TEST_ASSERT_TRUE(state.outcome==WifiOutcome::kFailed);
  }
  Fixture f; f.adapter.startOk=false; auto r=f.service.connect();
  TEST_ASSERT_TRUE(f.service.status(r.requestId).outcome==WifiOutcome::kFailed);
  TEST_ASSERT_EQUAL(1,f.adapter.offs);
}
void stale_ip_timeout_and_disconnect() {
  Fixture f; f.clock.now=UINT32_MAX-100;
  auto r=f.service.connect();
  f.adapter.snapshot={r.requestId+1,WifiLink::kConnected,"1.2.3.4"};
  f.service.tick(f.clock.now+29999); TEST_ASSERT_FALSE(f.service.isConnected());
  TEST_ASSERT_EQUAL(0,f.adapter.offs);
  f.service.tick(f.clock.now+30000);
  TEST_ASSERT_TRUE(f.service.status(r.requestId).outcome==WifiOutcome::kTimedOut);
  auto next=f.service.connect();
  f.adapter.snapshot.ip="0.0.0.0"; f.adapter.snapshot.link=WifiLink::kConnected;
  f.service.tick(f.clock.now); TEST_ASSERT_FALSE(f.service.isConnected());
  f.connected(); TEST_ASSERT_TRUE(f.service.isConnected());
  f.adapter.snapshot.link=WifiLink::kDisconnected;
  TEST_ASSERT_FALSE(f.service.isConnected()); f.service.tick(f.clock.now+1);
  TEST_ASSERT_TRUE(f.service.status(next.requestId).outcome==WifiOutcome::kDisconnected);
  TEST_ASSERT_TRUE(f.service.status(next.requestId).phase==WifiPhase::kOff);
  WifiServiceTestAccess::exhaust(f.service);
  TEST_ASSERT_TRUE(f.service.connect().availability==WifiAvailability::kIdsExhausted);
}
void invalid_ip_and_lost_identity() {
  Fixture f; auto r=f.service.connect();
  for (auto ip : {"", "0.0.0.0", "256.1.1.1", "1.2.3", "1.2.3.4.5", "1..3.4", "not-an-ip"}) {
    f.adapter.snapshot={r.requestId,WifiLink::kConnected,ip};
    f.service.tick(1); TEST_ASSERT_FALSE(f.service.isConnected());
    TEST_ASSERT_TRUE(f.service.status(r.requestId).phase==WifiPhase::kConnecting);
  }
  f.connected(); ++f.adapter.snapshot.requestId;
  f.service.tick(2);
  TEST_ASSERT_TRUE(f.service.status(r.requestId).outcome==WifiOutcome::kDisconnected);
  TEST_ASSERT_TRUE(f.service.status(r.requestId).phase==WifiPhase::kOff);
}
void module_lifecycle_and_quiet_after_close() {
  Fixture f;
  f.config.reload();
  TEST_ASSERT_TRUE(f.service.configurationStatus()==ConfigStatus::kOk);
  TEST_ASSERT_TRUE(f.service.canConnect()==WifiAvailability::kReady);
  TEST_ASSERT_FALSE(f.service.isConnected());
  TEST_ASSERT_EQUAL(0,f.adapter.connects);
  const auto request=f.service.connect();
  TEST_ASSERT_NOT_EQUAL(0,request.requestId);
  TEST_ASSERT_TRUE(f.service.status(request.requestId).phase==WifiPhase::kConnecting);
  f.connected();
  // A fake caller may use the link for longer than the connection deadline.
  // This proves ownership semantics, not actual radio or network availability.
  for (uint32_t now : {30000u,60000u,3600000u}) {
    f.service.tick(now);
    TEST_ASSERT_TRUE(f.service.isConnected());
    const auto state=f.service.status(request.requestId);
    TEST_ASSERT_TRUE(state.phase==WifiPhase::kConnected);
    TEST_ASSERT_TRUE(state.outcome==WifiOutcome::kSucceeded);
    TEST_ASSERT_EQUAL_STRING("192.168.1.2",state.ip.c_str());
    TEST_ASSERT_EQUAL(0,f.adapter.offs);
  }
  // The caller must explicitly close after success, including its error paths.
  const auto closed=f.service.close(request.requestId);
  TEST_ASSERT_TRUE(closed.phase==WifiPhase::kOff);
  TEST_ASSERT_TRUE(closed.outcome==WifiOutcome::kSucceeded);
  TEST_ASSERT_FALSE(f.service.isConnected());
  const int reads=f.store.reads;
  for (uint32_t now : {3600001u,86400000u,UINT32_MAX,0u,30000u}) {
    f.service.tick(now);
    TEST_ASSERT_TRUE(f.service.canConnect()==WifiAvailability::kReady);
    TEST_ASSERT_TRUE(f.service.close(request.requestId).phase==WifiPhase::kOff);
  }
  TEST_ASSERT_EQUAL(reads,f.store.reads);
  TEST_ASSERT_EQUAL(1,f.adapter.connects);
  TEST_ASSERT_EQUAL(1,f.adapter.disconnects);
  TEST_ASSERT_EQUAL(1,f.adapter.offs);
  TEST_ASSERT_EQUAL(0,f.adapter.scans);
}
void terminal_paths_release_ownership_without_retry() {
  for (int path=0;path<4;++path) {
    Fixture f;
    if (path==0) f.adapter.startOk=false;
    const auto request=f.service.connect();
    if (path==1) { f.adapter.snapshot.link=WifiLink::kFailed; f.service.tick(1); }
    if (path==2) f.service.tick(30000);
    if (path==3) f.service.close(request.requestId);
    const auto expected=path==2 ? WifiOutcome::kTimedOut : path==3 ? WifiOutcome::kCancelled : WifiOutcome::kFailed;
    TEST_ASSERT_TRUE(f.service.status(request.requestId).outcome==expected);
    TEST_ASSERT_TRUE(f.service.status(request.requestId).phase==WifiPhase::kOff);
    for (uint32_t now : {60000u,86400000u,UINT32_MAX,0u}) f.service.tick(now);
    TEST_ASSERT_EQUAL(1,f.adapter.connects);
    TEST_ASSERT_EQUAL(1,f.adapter.offs);
    TEST_ASSERT_TRUE(f.service.canConnect()==WifiAvailability::kReady);
    f.adapter.startOk=true;
    const auto next=f.service.connect();
    TEST_ASSERT_TRUE(next.requestId>request.requestId);
    TEST_ASSERT_TRUE(f.service.close(request.requestId).expired);
    TEST_ASSERT_TRUE(f.service.status(next.requestId).phase==WifiPhase::kConnecting);
    f.service.close(next.requestId);
    TEST_ASSERT_EQUAL(2,f.adapter.offs);
  }
}
int main(int,char**) { UNITY_BEGIN(); RUN_TEST(configuration_and_idle); RUN_TEST(ownership_and_saved_snapshot); RUN_TEST(failures_and_release_retry); RUN_TEST(stale_ip_timeout_and_disconnect); RUN_TEST(invalid_ip_and_lost_identity); RUN_TEST(module_lifecycle_and_quiet_after_close); RUN_TEST(terminal_paths_release_ownership_without_retry); return UNITY_END(); }
