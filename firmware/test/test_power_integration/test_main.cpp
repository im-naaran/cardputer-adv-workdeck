#include "../test_settings_controller/fixture.h"
#include "application/app_shell.h"
#include "application/codex/codex_controller.h"
#include "application/codex/codex_page.h"
#include "application/codex/codex_config_commands.h"
#include "application/actions/action_list_page.h"
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
#include "application/power/battery_service.h"
#include "application/power/screen_power_controller.h"
namespace adv {
struct DisplayTestAccess {
  static unsigned brightnessChanges(const DisplayAdapter& d) { return d.brightnessChanges_; }
  static unsigned frames(const DisplayAdapter& d) { return d.frames_; }
  static unsigned pushes(const DisplayAdapter& d) { return d.pushes_; }
  static uint8_t brightness(const DisplayAdapter& d) { return d.brightness_; }
  static unsigned brightnessAfterPush(const DisplayAdapter& d) { return d.pushesAtBrightness_; }
};
struct PowerKeyboard : KeyboardAdapter {
  InputSnapshot snapshot;
  void update() { updateSnapshot(snapshot); }
};
struct PowerBattery {
  unsigned reads{0}; BatterySnapshot value{70,false};
  BatterySnapshot read() { ++reads; return value; }
};
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
#define KeyboardAdapter PowerKeyboard
#define PlatformBatteryAdapter PowerBattery
#define ADV_SETTINGS_INTEGRATION_TEST
#define PlatformConfigFileStore SettingsIntegrationStore
#define MonotonicClock SettingsIntegrationClock
#define PlatformWifiAdapter SettingsIntegrationWifi
#include "../../src/main.cpp"
#undef PlatformConfigFileStore
#undef MonotonicClock
#undef PlatformWifiAdapter

#undef KeyboardAdapter
#undef PlatformBatteryAdapter
void actual_loop_power_lifecycle() {
  configStore.files["/config/display.json"] = encodeDisplayConfig({2,60});
  setup(); TEST_ASSERT_EQUAL(1,batteryAdapter.reads);
  loop(); TEST_ASSERT_EQUAL(1,DisplayTestAccess::pushes(display));
  const auto writes=configStore.writes;
  clockSource.now=59999; loop(); TEST_ASSERT_TRUE(screenPower.visible());
  clockSource.now=60000; loop(); TEST_ASSERT_FALSE(screenPower.visible());
  TEST_ASSERT_EQUAL(2,batteryAdapter.reads); TEST_ASSERT_EQUAL(0,DisplayTestAccess::brightness(display));
  const auto frames=DisplayTestAccess::frames(display), pushes=DisplayTestAccess::pushes(display);
  const auto brightnessChanges=DisplayTestAccess::brightnessChanges(display);
  for(int i=0;i<3;++i) { clockSource.now+=60000; redrawRequested=true; loop(); }
  TEST_ASSERT_EQUAL(frames,DisplayTestAccess::frames(display));
  TEST_ASSERT_EQUAL(pushes,DisplayTestAccess::pushes(display));
  TEST_ASSERT_EQUAL(5,batteryAdapter.reads); TEST_ASSERT_EQUAL(writes,configStore.writes);
  TEST_ASSERT_EQUAL(brightnessChanges,DisplayTestAccess::brightnessChanges(display));
  const auto previous=navigation.current();
  keyboard.snapshot.fn=true; keyboard.snapshot.pressed['4']=true;
  loop(); TEST_ASSERT_TRUE(screenPower.visible()); TEST_ASSERT_TRUE(navigation.current()==previous);
  TEST_ASSERT_EQUAL(pushes+1,DisplayTestAccess::pushes(display));
  TEST_ASSERT_EQUAL(pushes+1,DisplayTestAccess::brightnessAfterPush(display));
  TEST_ASSERT_EQUAL(102,DisplayTestAccess::brightness(display));
  loop(); TEST_ASSERT_TRUE(navigation.current()==previous);
  keyboard.snapshot={}; loop();
  keyboard.snapshot.fn=true; keyboard.snapshot.pressed['4']=true; loop();
  TEST_ASSERT_TRUE(navigation.current()==Module::kSettings);
  keyboard.snapshot={}; loop();
  TEST_ASSERT_EQUAL(5,batteryAdapter.reads);
  // An unused Wi-Fi request still times out and releases while the screen is off.
  settings.setField(0,"test-network"); TEST_ASSERT_TRUE(settings.test());
  screenPower.setTimeout(1); clockSource.now+=1000; loop();
  TEST_ASSERT_FALSE(screenPower.visible());
  wifiAdapter.offOk=false;
  clockSource.now+=30000; loop(); TEST_ASSERT_EQUAL(1,wifiAdapter.offs);
  TEST_ASSERT_TRUE(settings.operation().outcome==WifiOutcome::kTimedOut);
  wifiAdapter.offOk=true; clockSource.now+=1000; loop();
  TEST_ASSERT_EQUAL(2,wifiAdapter.offs); TEST_ASSERT_FALSE(screenPower.visible());
  TEST_ASSERT_TRUE(settings.operation().phase==WifiPhase::kOff);
  const auto offPushes=DisplayTestAccess::pushes(display);
  settings.setBrightness(4); TEST_ASSERT_EQUAL(0,DisplayTestAccess::brightness(display));
  keyboard.snapshot.ctrl=true; loop(); TEST_ASSERT_TRUE(screenPower.visible());
  TEST_ASSERT_EQUAL(204,DisplayTestAccess::brightness(display));
  TEST_ASSERT_EQUAL(offPushes+1,DisplayTestAccess::pushes(display));
  keyboard.snapshot={}; loop();
  settings.setAutoScreenOff(0);
  scheduler.setEnabled(ScheduledTaskId::kCodexUsageRefresh,false,clockSource.now);
  loop();
  const auto stablePushes=DisplayTestAccess::pushes(display);
  clockSource.now+=60000; loop();
  TEST_ASSERT_EQUAL(stablePushes,DisplayTestAccess::pushes(display));
}
void online_wake_never_executes_or_pastes() {
  ble.onConnected(); ble.poll(clockSource.now); handleConnection();
  Message hello; hello.actionId=protocol::kHelloAction; hello.resultCode="OK";
  hello.protocolVersion=protocol::kVersion; hello.computerId="test-host"; hello.computerName="Test";
  hello.capabilities={protocol::kActionsListAction,protocol::kActionsExecuteAction,protocol::kShortcutExecuteAction};
  hello.supportedActionTypes={ActionType::kScript,ActionType::kClipboard};
  router.route(hello); TEST_ASSERT_TRUE(session.ready());
  settings.setAutoScreenOff(60);
  for (auto type : {ActionType::kScript,ActionType::kClipboard}) {
    handleKey({type==ActionType::kScript ? Key::kDigit2 : Key::kDigit3,true},clockSource.now);
    Message page; page.actionId=protocol::kActionsListAction; page.resultCode="OK";
    page.hasActionType=true; page.actionType=type; page.total=1; page.execId=actions.directoryExecId(type);
    page.actions={{"test.action","测试条目","","",type}};
    router.route(page); TEST_ASSERT_EQUAL(1,actions.state(type).entries.size());
    keyboard.snapshot={}; loop(); clockSource.now+=60000; loop();
    TEST_ASSERT_FALSE(screenPower.visible());
    keyboard.snapshot.pressed['\r']=true; loop();
    TEST_ASSERT_TRUE(screenPower.visible()); TEST_ASSERT_TRUE(actions.executionExecId().empty());
    keyboard.snapshot={}; loop(); keyboard.snapshot.pressed['\r']=true; loop();
    TEST_ASSERT_FALSE(actions.executionExecId().empty());
    keyboard.snapshot={}; loop(); clockSource.now+=60000; loop();
    TEST_ASSERT_FALSE(screenPower.visible());
    TEST_ASSERT_TRUE(actions.executionExecId().empty());
    TEST_ASSERT_TRUE(actions.execution().status==ActionExecutionStatus::kUnconfirmed);
    // A global shortcut wakes but cannot submit a second action.
    keyboard.snapshot.alt=true; keyboard.snapshot.pressed['g']=true; loop();
    TEST_ASSERT_TRUE(actions.executionExecId().empty());
    keyboard.snapshot={}; loop();
  }
  clockSource.now+=60000; loop(); TEST_ASSERT_FALSE(screenPower.visible());
  const auto pushes=DisplayTestAccess::pushes(display);
  ble.onDisconnected(); loop();
  TEST_ASSERT_FALSE(session.ready()); TEST_ASSERT_FALSE(screenPower.visible());
  TEST_ASSERT_EQUAL(pushes,DisplayTestAccess::pushes(display));
  keyboard.snapshot.shift=true; loop(); TEST_ASSERT_TRUE(screenPower.visible());
}
int main(int,char**) { UNITY_BEGIN(); RUN_TEST(actual_loop_power_lifecycle); RUN_TEST(online_wake_never_executes_or_pastes); return UNITY_END(); }
