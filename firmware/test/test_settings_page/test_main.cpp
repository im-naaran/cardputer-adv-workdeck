#include "../test_settings_controller/fixture.h"
void tab_navigation_brightness_and_minutes() {
  SettingsFixture f;f.boot();InputConfig config;config.directionMapping[3]=false;f.router.applyConfig(config);
  f.key(Key::kUp);TEST_ASSERT_EQUAL(0,f.page.focus());f.key(Key::kEnter);
  TEST_ASSERT_TRUE(f.page.screen()==SettingsScreen::kBrightness);
  f.key(Key::kTab);TEST_ASSERT_EQUAL(4,f.controller.brightnessLevel());f.key(Key::kBackspace);
  f.key(Key::kTab);f.key(Key::kTab);f.key(Key::kEnter);
  f.key(Key::kTab);TEST_ASSERT_EQUAL_STRING("6",f.page.minutes().c_str());
  TEST_ASSERT_EQUAL(300,f.controller.intervalSeconds());
  f.page.tick(599);TEST_ASSERT_EQUAL(300,f.controller.intervalSeconds());
  f.page.tick(600);TEST_ASSERT_EQUAL(360,f.controller.intervalSeconds());
  f.key(Key::kEnter);TEST_ASSERT_TRUE(f.page.screen()==SettingsScreen::kCodex);
  f.key(Key::kLeft);f.key(Key::kBackspace);TEST_ASSERT_EQUAL(300,f.controller.intervalSeconds());
  f.page.render();
}
void legacy_seconds_no_implicit_save() {
  SettingsFixture f;f.store.files["/config/codex.json"]=encodeCodexConfig({90});f.boot();
  f.key(Key::kTab);f.key(Key::kTab);f.key(Key::kEnter);
  TEST_ASSERT_TRUE(f.page.minutes().empty());f.page.render();f.page.tick(5000);TEST_ASSERT_EQUAL(0,f.store.writes);
  f.key(Key::kEnter);TEST_ASSERT_EQUAL(90,f.controller.intervalSeconds());
  f.key(Key::kRight);f.page.leave();TEST_ASSERT_EQUAL(120,f.controller.intervalSeconds());
}
void autosave_coalesces_and_retries_after_failure() {
  SettingsFixture f;f.boot();f.key(Key::kTab);f.key(Key::kTab);f.key(Key::kEnter);
  const auto adjust=[&](uint32_t now) { f.page.handle(Module::kSettings,{InputAction::kDirection,{Key::kRight}},now); };
  adjust(0xfffffff0u);f.page.tick(50);TEST_ASSERT_EQUAL(0,f.store.writes);
  adjust(100);f.page.tick(650);TEST_ASSERT_EQUAL(0,f.store.writes);
  f.store.failWrite=true;f.page.tick(700);TEST_ASSERT_EQUAL(1,f.store.writes);
  TEST_ASSERT_EQUAL(300,f.controller.intervalSeconds());TEST_ASSERT_EQUAL_STRING("7",f.page.minutes().c_str());
  f.page.tick(20000);TEST_ASSERT_EQUAL(1,f.store.writes);f.page.render();
  f.store.failWrite=false;f.page.leave();TEST_ASSERT_EQUAL(420,f.controller.intervalSeconds());
  TEST_ASSERT_EQUAL(2,f.store.writes);
}

void text_symbols_utf8_limits_and_page_preservation() {
  SettingsFixture f;f.boot();f.enterWifi();f.controller.setField(0,"中文");f.key(Key::kEnter);
  TEST_ASSERT_TRUE(f.page.screen()==SettingsScreen::kWifi);
  f.key(Key::kBackspace);TEST_ASSERT_EQUAL_STRING("中",f.page.editor().c_str());f.type(" Aa1;,. /!@ ");
  const auto text=f.page.editor();f.page.render();
  TEST_ASSERT_FALSE(f.page.handle(Module::kCodex,{InputAction::kConfirm,{Key::kEnter}}));
  TEST_ASSERT_EQUAL_STRING(text.c_str(),f.page.editor().c_str());f.key(Key::kEnter);
  TEST_ASSERT_EQUAL_STRING(text.c_str(),f.controller.draft().ssid.c_str());
  f.controller.setField(0,std::string(32,'a'));f.key(Key::kEnter);f.type("b");TEST_ASSERT_EQUAL(32,f.page.editor().size());
  f.key(Key::kTab);TEST_ASSERT_EQUAL(1,f.page.focus());TEST_ASSERT_TRUE(f.page.textEditing(Module::kSettings));f.type(std::string(64,'x'));f.type("z");
  TEST_ASSERT_EQUAL(64,f.page.editor().size());f.key(Key::kEnter);TEST_ASSERT_EQUAL(64,f.controller.draft().username.size());
}
void network_info_and_scan_selection() {
  SettingsFixture f;f.boot();f.enterWifi();f.controller.setField(2,std::string(64,'p'));
  for(int i=0;i<6;++i)f.key(Key::kTab);f.key(Key::kEnter);TEST_ASSERT_TRUE(f.page.screen()==SettingsScreen::kViewer);
  f.page.render();f.key(Key::kEnter);TEST_ASSERT_EQUAL(6,f.page.focus());
  f.key(Key::kBackspace);f.key(Key::kEnter);for(int i=0;i<3;++i)f.key(Key::kTab);f.key(Key::kEnter);
  f.adapter.networks={{"network",-30}};f.adapter.scanState=1;f.controller.tick(1);f.page.render();f.key(Key::kEnter);
  TEST_ASSERT_TRUE(f.page.screen()==SettingsScreen::kWifi);TEST_ASSERT_EQUAL_STRING("network",f.controller.draft().ssid.c_str());
  TEST_ASSERT_EQUAL(64,f.controller.draft().password.size());
}
void bounds_and_network_retry() {
  SettingsFixture f;f.boot();f.key(Key::kEnter);
  const int writes=f.store.writes;f.key(Key::kEnter);TEST_ASSERT_EQUAL(writes,f.store.writes);
  f.key(Key::kBackspace);f.key(Key::kTab);f.key(Key::kTab);f.key(Key::kEnter);
  for(int i=0;i<80;++i)f.key(Key::kRight);
  TEST_ASSERT_EQUAL_STRING("60",f.page.minutes().c_str());f.key(Key::kBackspace);
  TEST_ASSERT_EQUAL(3600,f.controller.intervalSeconds());
  f.key(Key::kEnter);for(int i=0;i<80;++i)f.key(Key::kLeft);
  TEST_ASSERT_EQUAL_STRING("1",f.page.minutes().c_str());f.key(Key::kBackspace);
  f.key(Key::kUp);f.key(Key::kEnter);
  f.controller.setField(0,"network");
  for(int i=0;i<5;++i)f.key(Key::kTab);f.key(Key::kEnter);
  TEST_ASSERT_TRUE(f.page.screen()==SettingsScreen::kViewer);
  f.adapter.offOk=false;f.adapter.snapshot.link=WifiLink::kConnected;f.adapter.snapshot.ip="192.168.1.8";
  f.controller.tick(1);f.page.render();
  TEST_ASSERT_TRUE(f.controller.operation().phase==WifiPhase::kReleaseFailed);
  f.adapter.offOk=true;f.key(Key::kEnter);
  TEST_ASSERT_TRUE(f.controller.operation().phase==WifiPhase::kOff);
  f.key(Key::kBackspace);TEST_ASSERT_EQUAL(5,f.page.focus());
}
int main(int,char**) { UNITY_BEGIN();RUN_TEST(tab_navigation_brightness_and_minutes);RUN_TEST(legacy_seconds_no_implicit_save);RUN_TEST(text_symbols_utf8_limits_and_page_preservation);RUN_TEST(network_info_and_scan_selection);RUN_TEST(autosave_coalesces_and_retries_after_failure);RUN_TEST(bounds_and_network_retry);return UNITY_END(); }
