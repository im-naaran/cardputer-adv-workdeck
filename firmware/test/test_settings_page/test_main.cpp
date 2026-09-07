#include "../test_settings_controller/fixture.h"
void tab_navigation_brightness_and_minutes() {
  SettingsFixture f;f.boot();InputConfig config;config.directionMapping[3]=false;f.router.applyConfig(config);
  f.key(Key::kUp);TEST_ASSERT_EQUAL(0,f.page.focus());f.key(Key::kEnter);
  TEST_ASSERT_TRUE(f.page.screen()==SettingsScreen::kBrightness);
  f.key(Key::kTab);TEST_ASSERT_EQUAL(4,f.controller.brightnessLevel());f.key(Key::kBackspace);
  f.key(Key::kTab);f.key(Key::kTab);f.key(Key::kEnter);f.key(Key::kEnter);
  f.key(Key::kBackspace);f.type("0");f.key(Key::kTab);f.key(Key::kEnter);
  TEST_ASSERT_EQUAL_STRING("0",f.page.minutes().c_str());TEST_ASSERT_EQUAL(300,f.controller.intervalSeconds());
  f.key(Key::kUp);f.key(Key::kEnter);f.key(Key::kBackspace);f.type("60");f.key(Key::kTab);f.key(Key::kEnter);
  TEST_ASSERT_EQUAL(3600,f.controller.intervalSeconds());f.page.render();
}
void legacy_seconds_no_implicit_save() {
  SettingsFixture f;f.store.files["/config/codex.json"]=encodeCodexConfig({90});f.boot();
  f.key(Key::kTab);f.key(Key::kTab);f.key(Key::kEnter);
  TEST_ASSERT_TRUE(f.page.minutes().empty());f.page.render();TEST_ASSERT_EQUAL(0,f.store.writes);
  f.key(Key::kTab);f.key(Key::kEnter);TEST_ASSERT_EQUAL(90,f.controller.intervalSeconds());
}
void text_symbols_utf8_limits_and_page_preservation() {
  SettingsFixture f;f.boot();f.enterWifi();f.controller.setField(0,"中文");f.key(Key::kEnter);
  f.key(Key::kBackspace);TEST_ASSERT_EQUAL_STRING("中",f.page.editor().c_str());f.type(" Aa1;,. /!@ ");
  const auto text=f.page.editor();f.page.render();
  TEST_ASSERT_FALSE(f.page.handle(Module::kCodex,{InputAction::kConfirm,{Key::kEnter}}));
  TEST_ASSERT_EQUAL_STRING(text.c_str(),f.page.editor().c_str());f.key(Key::kEnter);
  TEST_ASSERT_EQUAL_STRING(text.c_str(),f.controller.draft().ssid.c_str());
  f.controller.setField(0,std::string(32,'a'));f.key(Key::kEnter);f.type("b");TEST_ASSERT_EQUAL(32,f.page.editor().size());
  f.key(Key::kTab);TEST_ASSERT_EQUAL(1,f.page.focus());f.key(Key::kEnter);f.type(std::string(64,'x'));f.type("z");
  TEST_ASSERT_EQUAL(64,f.page.editor().size());f.key(Key::kEnter);TEST_ASSERT_EQUAL(64,f.controller.draft().username.size());
}
void full_view_and_scan_selection() {
  SettingsFixture f;f.boot();f.enterWifi();f.controller.setField(2,std::string(64,'p'));
  for(int i=0;i<9;++i)f.key(Key::kTab);f.key(Key::kEnter);TEST_ASSERT_TRUE(f.page.screen()==SettingsScreen::kViewer);
  f.page.render();f.key(Key::kTab);TEST_ASSERT_EQUAL(1,f.page.viewPage());f.key(Key::kEnter);TEST_ASSERT_EQUAL(9,f.page.focus());
  f.key(Key::kBackspace);f.key(Key::kEnter);for(int i=0;i<3;++i)f.key(Key::kTab);f.key(Key::kEnter);
  f.adapter.networks={{"network",-30}};f.adapter.scanState=1;f.controller.tick(1);f.page.render();f.key(Key::kEnter);
  TEST_ASSERT_TRUE(f.page.screen()==SettingsScreen::kWifi);TEST_ASSERT_EQUAL_STRING("network",f.controller.draft().ssid.c_str());
  TEST_ASSERT_EQUAL(64,f.controller.draft().password.size());
}
int main(int,char**) { UNITY_BEGIN();RUN_TEST(tab_navigation_brightness_and_minutes);RUN_TEST(legacy_seconds_no_implicit_save);RUN_TEST(text_symbols_utf8_limits_and_page_preservation);RUN_TEST(full_view_and_scan_selection);return UNITY_END(); }
