#include "fixture.h"
void brightness_persistence_and_retry() {
  SettingsFixture f; f.boot(); TEST_ASSERT_EQUAL(153,f.brightness);
  f.controller.setBrightness(4); TEST_ASSERT_EQUAL(204,f.brightness);
  const int writes=f.store.writes; f.controller.setBrightness(4); TEST_ASSERT_EQUAL(writes,f.store.writes);
  f.store.failWrite=true; f.controller.setBrightness(5);
  TEST_ASSERT_EQUAL(255,f.brightness); TEST_ASSERT_EQUAL(4,f.displayConfig.saved().brightnessLevel);
  contains(f.controller.displayMessage(),"当前生效"); contains(f.controller.displayMessage(),"保存失败");
  f.store.failWrite=false; f.controller.setBrightness(5); TEST_ASSERT_EQUAL(5,f.displayConfig.saved().brightnessLevel);
  const auto applies=f.applies;f.controller.setBrightness(0);f.controller.setBrightness(6);TEST_ASSERT_EQUAL(applies,f.applies);
  f.store.failReload=true; f.controller.setBrightness(1); contains(f.controller.displayMessage(),"文件可能已保存");
  TEST_ASSERT_EQUAL(51,f.brightness);
}
void minutes_validation_and_legacy() {
  SettingsFixture f; f.store.files["/config/codex.json"]=encodeCodexConfig({90}); f.boot();
  TEST_ASSERT_EQUAL(90,f.controller.intervalSeconds()); TEST_ASSERT_EQUAL(0,f.store.writes);
  for (const auto* text:{"","0","-1","1.5","x","61"," 5","999999999999999999999999"})
    TEST_ASSERT_FALSE(f.controller.saveMinutes(text));
  TEST_ASSERT_EQUAL(0,f.store.writes); TEST_ASSERT_EQUAL(90,f.controller.intervalSeconds());
  f.scheduler.setEnabled(ScheduledTaskId::kCodexUsageRefresh,false,0);
  for (const auto* text:{"1","5","60"}) TEST_ASSERT_TRUE(f.controller.saveMinutes(text));
  TEST_ASSERT_EQUAL(3600,f.controller.intervalSeconds()); TEST_ASSERT_FALSE(f.codex.taskState().enabled);
  f.store.failWrite=true; TEST_ASSERT_FALSE(f.controller.saveMinutes("1")); TEST_ASSERT_EQUAL(3600,f.controller.intervalSeconds());
  f.store.failWrite=false; f.scheduler.cancel(ScheduledTaskId::kCodexUsageRefresh);
  TEST_ASSERT_FALSE(f.controller.saveMinutes("1")); contains(f.controller.codexMessage(),"已保存但应用失败");
}
void wifi_drafts_save_test_and_history() {
  SettingsFixture f; f.boot(); f.controller.setField(0," Office "); f.controller.setField(1,"user"); f.controller.setField(2,"p");
  TEST_ASSERT_TRUE(f.controller.saveWifi()); TEST_ASSERT_EQUAL(0,f.adapter.connects);TEST_ASSERT_EQUAL(0,f.adapter.scans);
  TEST_ASSERT_TRUE(f.controller.test()); const int writes=f.store.writes;
  f.controller.setField(2,"new");TEST_ASSERT_EQUAL_STRING("p",f.adapter.credentials.password.c_str());
  f.adapter.snapshot.link=WifiLink::kConnected;f.adapter.snapshot.ip="192.168.1.2";
  TEST_ASSERT_TRUE(f.controller.tick(1));TEST_ASSERT_EQUAL(1,f.adapter.offs);TEST_ASSERT_EQUAL(writes,f.store.writes);
  contains(f.controller.wifiDetails(),"配置已修改"); contains(f.controller.wifiDetails(),"测试IP：192.168.1.2");
  contains(f.controller.wifiDetails(),"Wi-Fi已关闭"); TEST_ASSERT_FALSE(f.wifi.isConnected());
  f.controller.setField(1,""); TEST_ASSERT_FALSE(f.controller.saveWifi());
  TEST_ASSERT_EQUAL_STRING("user",f.wifiConfig.saved().username.c_str());
  f.controller.setField(2,"password");TEST_ASSERT_TRUE(f.controller.saveWifi());
  f.controller.setField(2,"new pass");TEST_ASSERT_TRUE(f.controller.test());contains(f.controller.wifiDetails(),"测试配置未保存");
}
void wifi_scan_busy_and_release_failure() {
  SettingsFixture f;f.boot();f.controller.setField(1,"user");f.controller.setField(2,"p");
  TEST_ASSERT_TRUE(f.controller.scan()); TEST_ASSERT_FALSE(f.controller.scan()); contains(f.controller.wifiMessage(),"忙");
  f.adapter.networks={{"中文网络",-20}};f.adapter.scanState=1;f.adapter.offOk=false;
  f.controller.tick(1); TEST_ASSERT_EQUAL(1,f.controller.scans().count);
  f.controller.selectNetwork(0);TEST_ASSERT_EQUAL_STRING("中文网络",f.controller.draft().ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("user",f.controller.draft().username.c_str());TEST_ASSERT_EQUAL_STRING("p",f.controller.draft().password.c_str());
  contains(f.controller.wifiSummary(),"关闭失败"); f.adapter.offOk=true;f.controller.retryClose();
  TEST_ASSERT_TRUE(f.controller.operation().phase==WifiPhase::kOff);
  f.controller.saveWifi();auto request=f.wifi.connect();TEST_ASSERT_NOT_EQUAL(0,request.requestId);
  TEST_ASSERT_FALSE(f.controller.test());const auto offs=f.adapter.offs;f.controller.retryClose();TEST_ASSERT_EQUAL(offs,f.adapter.offs);
}
void startup_errors_and_refresh_keep_drafts() {
  SettingsFixture f; f.store.files["/config/display.json"]="bad";f.store.error=ConfigStatus::kNotMounted;f.boot();
  TEST_ASSERT_EQUAL(153,f.brightness);contains(f.controller.displayMessage(),"未挂载");
  TEST_ASSERT_EQUAL(0,f.adapter.connects); TEST_ASSERT_EQUAL(0,f.adapter.offs);
  f.store.error=ConfigStatus::kOk;f.controller.setField(0,"draft");
  f.store.files["/config/wifi.json"]=encodeWifiConfig({"external","",""});f.controller.refreshWifi();
  TEST_ASSERT_EQUAL_STRING("draft",f.controller.draft().ssid.c_str());
  f.controller.setField(0,"external");f.store.files["/config/wifi.json"]=encodeWifiConfig({"latest","",""});f.controller.refreshWifi();
  TEST_ASSERT_EQUAL_STRING("latest",f.controller.draft().ssid.c_str());
  f.store.error=ConfigStatus::kReadFailed;f.controller.refreshWifi();TEST_ASSERT_EQUAL_STRING("latest",f.controller.draft().ssid.c_str());
}
void external_release_redraw_and_save_feedback() {
  SettingsFixture f;f.boot();f.controller.setField(0,"saved");TEST_ASSERT_TRUE(f.controller.saveWifi());
  const auto request=f.wifi.connect();TEST_ASSERT_TRUE(f.controller.tick(1));
  TEST_ASSERT_FALSE(f.controller.tick(2));f.wifi.close(request.requestId);
  TEST_ASSERT_TRUE(f.controller.tick(3));TEST_ASSERT_FALSE(f.controller.tick(4));
  f.controller.setField(0,"test");f.store.failWrite=true;TEST_ASSERT_FALSE(f.controller.saveWifi());
  TEST_ASSERT_TRUE(f.controller.test());f.controller.tick(30000);
  contains(f.controller.wifiMessage(),"超时");contains(f.controller.wifiDetails(),"上次保存：保存失败");
  contains(f.controller.wifiDetails(),"测试配置未保存");
}
int main(int,char**) { UNITY_BEGIN(); RUN_TEST(brightness_persistence_and_retry);RUN_TEST(minutes_validation_and_legacy);RUN_TEST(wifi_drafts_save_test_and_history);RUN_TEST(wifi_scan_busy_and_release_failure);RUN_TEST(startup_errors_and_refresh_keep_drafts);RUN_TEST(external_release_redraw_and_save_feedback);return UNITY_END(); }
