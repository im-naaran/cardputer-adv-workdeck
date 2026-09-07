#include "../test_settings_controller/fixture.h"
#include "application/app_shell.h"
#include "application/codex/codex_controller.h"
#include "application/codex/codex_page.h"
#include "application/codex/codex_config_commands.h"
#include "application/placeholder_page.h"
#include "application/scripts/scripts_page.h"
#include "application/input/input_config_commands.h"
#include "application/time_sync/time_sync_controller.h"
#include "core/connection_session.h"
#include "core/message_codec.h"
#include "core/message_router.h"
#include "core/navigation_service.h"
#include "core/protocol_constants.h"
#include "platform/ble_transport.h"
#include "platform/display_adapter.h"
#include "application/settings/settings_page.h"
#include "platform/keyboard_adapter.h"
#include "platform/monotonic_clock.h"
namespace adv {
struct SettingsIntegrationStore : ::SettingsStore { bool begin() { return true; } };
struct SettingsIntegrationClock : ::Clock {};
struct SettingsIntegrationWifi : ::Adapter {};
}
struct TestSerial {
  void begin(int) {}
  template <typename... T> void printf(const char*, T...) {}
  void println(const char*) {}
  int available() { return 0; }
  int read() { return -1; }
} Serial;
void delay(int) {}
// Compile the actual setup, input dispatch, connection handling and loop against
// fakes. No duplicate test loop can silently drift away from production ordering.
#define ADV_SETTINGS_INTEGRATION_TEST
#define PlatformConfigFileStore SettingsIntegrationStore
#define MonotonicClock SettingsIntegrationClock
#define PlatformWifiAdapter SettingsIntegrationWifi
#include "../../src/main.cpp"
#undef PlatformConfigFileStore
#undef MonotonicClock
#undef PlatformWifiAdapter

void actual_main_offline_lifecycle() {
  configStore.files["/config/display.json"] = encodeDisplayConfig({2});
  configStore.files["/config/codex.json"] = encodeCodexConfig({90});
  configStore.files["/config/wifi.json"] = encodeWifiConfig({"saved","","password"});
  setup();
  TEST_ASSERT_EQUAL(2,settings.brightnessLevel());TEST_ASSERT_EQUAL(90,settings.intervalSeconds());
  TEST_ASSERT_EQUAL_STRING("saved",settings.draft().ssid.c_str());
  TEST_ASSERT_EQUAL(0,wifiAdapter.connects);TEST_ASSERT_EQUAL(0,wifiAdapter.scans);TEST_ASSERT_EQUAL(0,wifiAdapter.offs);
  handleKey({Key::kDigit4,true},0);TEST_ASSERT_TRUE(navigation.current()==Module::kSettings);
  TEST_ASSERT_FALSE(session.ready());handleKey({Key::kTab},0);handleKey({Key::kEnter},0);handleKey({Key::kEnter},0);
  KeyEvent symbol{Key::kCharacter};symbol.character=';';symbol.text=';';handleKey(symbol,0);
  TEST_ASSERT_EQUAL_STRING("saved;",settingsPage.editor().c_str());
  const bool enabled=codex.taskState().enabled;handleKey({Key::kEnter,true},0);TEST_ASSERT_EQUAL(enabled,codex.taskState().enabled);
  KeyEvent shortcut{Key::kCharacter};shortcut.character='a';shortcut.alt=true;handleKey(shortcut,0);
  TEST_ASSERT_EQUAL_STRING("saved;",settingsPage.editor().c_str());
  TEST_ASSERT_FALSE(scripts.state().feedback.empty());
  handleKey({Key::kDigit1,true},0);TEST_ASSERT_TRUE(navigation.current()==Module::kCodex);
  handleKey({Key::kEnter},0); // Offline Codex input must stay gated.
  handleKey({Key::kDigit4,true},0);TEST_ASSERT_EQUAL_STRING("saved;",settingsPage.editor().c_str());
  handleKey({Key::kEnter},0);TEST_ASSERT_TRUE(settings.saveWifi());
  TEST_ASSERT_EQUAL(0,wifiAdapter.connects);TEST_ASSERT_TRUE(settings.test());
  handleKey({Key::kDigit2,true},0);
  ble.onConnected();handleConnection();ble.onDisconnected();handleConnection();
  TEST_ASSERT_EQUAL_STRING("saved;",settings.draft().ssid.c_str());
  wifiAdapter.snapshot.link=WifiLink::kConnected;wifiAdapter.snapshot.ip="192.168.1.4";
  clockSource.now=10;loop();TEST_ASSERT_EQUAL(1,wifiAdapter.offs);TEST_ASSERT_FALSE(wifi.isConnected());
  contains(settings.wifiDetails(),"测试IP：192.168.1.4");
  const int reads=configStore.reads;const int writes=configStore.writes;
  for (int i=0;i<20;++i) { clockSource.now+=10000;loop();settings.wifiSummary();wifi.canConnect(); }
  TEST_ASSERT_EQUAL(reads,configStore.reads);TEST_ASSERT_EQUAL(writes,configStore.writes);TEST_ASSERT_EQUAL(1,wifiAdapter.connects);
  // Saving device settings neither cancels a query nor re-enables a disabled task.
  ble.onConnected();ble.poll(clockSource.now);handleConnection();
  codex.onSessionReady(true,clockSource.now);TEST_ASSERT_TRUE(codex.state().inFlight());
  scheduler.setEnabled(ScheduledTaskId::kCodexUsageRefresh,false,clockSource.now);
  TEST_ASSERT_TRUE(settings.saveMinutes("1"));TEST_ASSERT_TRUE(codex.state().inFlight());TEST_ASSERT_FALSE(codex.taskState().enabled);
  TEST_ASSERT_EQUAL_STRING("saved;",parseWifiConfig(configStore.files["/config/wifi.json"]).config.ssid.c_str());
  handleKey({Key::kDigit4,true},clockSource.now);redrawRequested=true;draw(clockSource.now);
  // A future module uses the same singleton and actual loop as settings tests.
  const auto moduleRequest=wifi.connect();
  TEST_ASSERT_NOT_EQUAL(0,moduleRequest.requestId);
  TEST_ASSERT_FALSE(settings.test());TEST_ASSERT_FALSE(settings.scan());
  contains(settings.wifiMessage(),"忙");
  const int previousOffs=wifiAdapter.offs;
  settings.retryClose();TEST_ASSERT_EQUAL(previousOffs,wifiAdapter.offs);
  wifiAdapter.snapshot.link=WifiLink::kConnected;wifiAdapter.snapshot.ip="192.168.1.5";
  loop();
  TEST_ASSERT_TRUE(wifi.status(moduleRequest.requestId).phase==WifiPhase::kConnected);
  handleKey({Key::kDigit3,true},clockSource.now);
  ble.onDisconnected();handleConnection();
  for (int i=0;i<4;++i) { clockSource.now+=60000;loop();TEST_ASSERT_TRUE(wifi.isConnected()); }
  TEST_ASSERT_EQUAL(previousOffs,wifiAdapter.offs);
  settings.setField(0,"next-network");TEST_ASSERT_TRUE(settings.saveWifi());
  TEST_ASSERT_EQUAL_STRING("saved;",wifiAdapter.credentials.ssid.c_str());
  // Caller finishes its work, explicitly releases, and verifies a quiet loop.
  TEST_ASSERT_TRUE(wifi.close(moduleRequest.requestId).phase==WifiPhase::kOff);
  const int closedReads=configStore.reads;const int closedConnects=wifiAdapter.connects;
  for (int i=0;i<24;++i) { clockSource.now+=3600000;loop(); }
  TEST_ASSERT_EQUAL(closedReads,configStore.reads);TEST_ASSERT_EQUAL(closedConnects,wifiAdapter.connects);
  TEST_ASSERT_EQUAL(previousOffs+1,wifiAdapter.offs);
  const auto next=wifi.connect();
  TEST_ASSERT_EQUAL_STRING("next-network",wifiAdapter.credentials.ssid.c_str());
  TEST_ASSERT_TRUE(wifi.close(moduleRequest.requestId).expired);
  TEST_ASSERT_TRUE(wifi.close(next.requestId).phase==WifiPhase::kOff);

}
int main(int,char**) { UNITY_BEGIN();RUN_TEST(actual_main_offline_lifecycle);return UNITY_END(); }
