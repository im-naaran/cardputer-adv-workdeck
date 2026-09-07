#include "../test_wifi_service/fixture.h"
void draft_test_and_cleanup() {
  Fixture f; const auto saved=f.store.bytes;
  TEST_ASSERT_TRUE(f.service.test({}).availability==WifiAvailability::kInvalidConfig);
  WifiConfig draft{"draft","u","x"}; auto r=f.service.test(draft); draft.ssid="edited";
  TEST_ASSERT_EQUAL_STRING("draft",f.adapter.credentials.ssid.c_str());
  f.connected(); auto status=f.service.status(r.requestId);
  TEST_ASSERT_TRUE(status.outcome==WifiOutcome::kSucceeded); TEST_ASSERT_TRUE(status.phase==WifiPhase::kOff);
  TEST_ASSERT_EQUAL_STRING("draft",status.ssid.c_str()); TEST_ASSERT_EQUAL_STRING("192.168.1.2",status.ip.c_str());
  TEST_ASSERT_FALSE(f.service.isConnected()); TEST_ASSERT_EQUAL(0,f.store.writes);
  TEST_ASSERT_EQUAL_STRING(saved.c_str(),f.store.bytes.c_str());
  f.service.tick(100000); TEST_ASSERT_EQUAL(1,f.adapter.connects);
  for(bool fail:{false,true}) {
    auto next=f.service.test({"n","","12345678"});
    if(fail) f.adapter.snapshot.link=WifiLink::kFailed;
    f.service.tick(30000);
    TEST_ASSERT_TRUE(f.service.status(next.requestId).phase==WifiPhase::kOff);
    TEST_ASSERT_TRUE(f.service.status(next.requestId).outcome==(fail?WifiOutcome::kFailed:WifiOutcome::kTimedOut));
  }
}
void scan_results_and_capacity() {
  Fixture f; auto r=f.service.scan();
  TEST_ASSERT_EQUAL(0,f.adapter.connects);
  f.adapter.networks={{"",-1},{std::string(33,'x'),-1},{"dup",-90},{"dup",-10},{"中文",-20}};
  for(int i=0;i<40;++i) f.adapter.networks.push_back({"net"+std::to_string(i),-30-i});
  f.adapter.scanState=f.adapter.networks.size(); f.service.tick(1);
  auto& results=f.service.scanResults();
  TEST_ASSERT_EQUAL(32,results.count); TEST_ASSERT_TRUE(results.truncated);
  TEST_ASSERT_EQUAL_STRING("dup",results.networks[0].ssid.c_str());
  TEST_ASSERT_EQUAL(-10,results.networks[0].rssi);
  TEST_ASSERT_EQUAL_STRING("中文",results.networks[1].ssid.c_str());
  TEST_ASSERT_TRUE(f.adapter.networks.empty()); TEST_ASSERT_EQUAL(1,f.adapter.stops); TEST_ASSERT_EQUAL(1,f.adapter.offs);
  TEST_ASSERT_TRUE(f.service.status(r.requestId).outcome==WifiOutcome::kSucceeded);
  TEST_ASSERT_EQUAL(0,f.store.reads); TEST_ASSERT_EQUAL(0,f.store.writes);
}
void scan_terminal_paths() {
  for(int outcome:{0,-1,-2}) {
    Fixture f; f.clock.now=UINT32_MAX-10; auto r=f.service.scan();
    TEST_ASSERT_TRUE(f.service.scan().availability==WifiAvailability::kBusy);
    TEST_ASSERT_TRUE(f.service.connect().availability==WifiAvailability::kBusy);
    f.adapter.scanState=outcome;
    f.service.tick(f.clock.now+15000);
    TEST_ASSERT_TRUE(f.service.status(r.requestId).phase==WifiPhase::kOff);
    TEST_ASSERT_TRUE(f.service.status(r.requestId).outcome==(outcome==0?WifiOutcome::kSucceeded:outcome==-1?WifiOutcome::kTimedOut:WifiOutcome::kFailed));
    TEST_ASSERT_EQUAL(1,f.adapter.stops); TEST_ASSERT_EQUAL(1,f.adapter.offs);
  }
  Fixture f; auto r=f.service.scan(); f.adapter.stopOk=false;
  TEST_ASSERT_TRUE(f.service.close(r.requestId).phase==WifiPhase::kReleaseFailed);
  TEST_ASSERT_TRUE(f.service.scan().availability==WifiAvailability::kReleaseFailed);
  f.adapter.stopOk=true;
  TEST_ASSERT_TRUE(f.service.close(r.requestId).phase==WifiPhase::kOff);
  f.adapter.startOk=false; auto next=f.service.scan();
  TEST_ASSERT_TRUE(f.service.status(next.requestId).outcome==WifiOutcome::kFailed);
  TEST_ASSERT_TRUE(f.service.close(r.requestId).expired);
}
int main(int,char**) { UNITY_BEGIN(); RUN_TEST(draft_test_and_cleanup); RUN_TEST(scan_results_and_capacity); RUN_TEST(scan_terminal_paths); return UNITY_END(); }
