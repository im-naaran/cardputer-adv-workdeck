#if defined(ARDUINO) || defined(ADV_SETTINGS_INTEGRATION_TEST)
#ifdef ARDUINO
#include <Arduino.h>
#endif

#include "application/app_shell.h"
#include "application/power/battery_service.h"
#include "application/power/screen_power_controller.h"
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

namespace {
adv::DisplayAdapter display;
adv::ScreenPowerController screenPower;
bool backlightOn = true;
uint8_t userBrightness = 153;
void applyDisplayConfig(const adv::DisplayConfig& config) {
  screenPower.setTimeout(config.autoScreenOffSeconds);
  userBrightness = config.brightnessLevel * 51;
  // Applying a saved value must not light an off-screen or pending wake frame.
  if (backlightOn && screenPower.visible()) display.setBrightness(userBrightness);
}
adv::PlatformBatteryAdapter batteryAdapter;
adv::BatteryService battery([] { return batteryAdapter.read(); });
adv::KeyboardAdapter keyboard;
adv::MonotonicClock clockSource;
adv::BleTransport ble;
adv::ConnectionSession session;
adv::NavigationService navigation;
adv::InputRouter inputRouter;
adv::AppShell shell;
adv::CodexPage codexPage;
adv::ActionListPage scriptsPage(adv::ActionType::kScript);
adv::ActionListPage clipboardPage(adv::ActionType::kClipboard);
adv::MessageCodec codec;
adv::MessageRouter router;
adv::ScheduledTaskService scheduler;
adv::ExecIdGenerator execIds;
adv::PlatformSystemClock systemClock;
adv::SystemTimeService systemTime(systemClock);
// All controllers submit through the same bounded BLE queue.
bool enqueueRequest(const std::string& id, const std::string& jsonl, uint32_t now) {
  return ble.enqueueRequest(id, jsonl, now);
}

void cancelPending(const std::string& id) {
  ble.cancelPending(id);
}

adv::TimeSyncController timeSync(scheduler, execIds, systemTime, enqueueRequest, cancelPending);
adv::CodexController codex(scheduler, execIds, enqueueRequest, cancelPending);
adv::ActionsController actions(execIds, enqueueRequest, cancelPending);
adv::PlatformConfigFileStore configStore;
adv::InputConfigService inputConfig(configStore, inputRouter);
adv::CodexConfigService codexConfig(configStore, codex, [] { return clockSource.nowMs(); });
adv::DisplayConfigService displayConfig(configStore);
adv::PlatformPowerAdapter powerAdapter;
adv::PowerConfigService powerConfig(configStore, powerAdapter);
adv::WifiConfigService wifiConfig(configStore);
adv::PlatformWifiAdapter wifiAdapter;
adv::WifiService wifi(wifiConfig, wifiAdapter, clockSource);
adv::SettingsController settings(displayConfig, codexConfig, codex, wifiConfig, wifi, powerConfig,
                                 applyDisplayConfig);
adv::SettingsPage settingsPage(settings, display);


void printTimeDiagnostic() {
  int64_t utc = 0;
  const bool available = systemTime.currentUtcMilliseconds(utc);
  Serial.printf("time synced=%d utc_ms=%lld offset_min=%d last_sync_ms=%lu now_ms=%lu\n",
                available, static_cast<long long>(utc), systemTime.utcOffsetMinutes(),
                static_cast<unsigned long>(systemTime.lastSuccessfulSyncMs()),
                static_cast<unsigned long>(clockSource.nowMs()));
}

void printConfigResult(const std::string& line) { Serial.println(line.c_str()); }
adv::CodexConfigCommands codexCommands(codexConfig, codex, printConfigResult);
adv::InputConfigCommands inputCommands(inputConfig, printConfigResult);
// Exactly one consumer owns the serial byte stream and dispatches complete lines.
adv::ConfigCommandDispatcher configCommands(
    [](const std::string& line) { return codexCommands.execute(line); },
    [](const std::string& line) { return inputCommands.execute(line); },
    printConfigResult, printTimeDiagnostic);
bool redrawRequested = true;

void clearModuleSession() {
  codexPage.invalidateTimeSnapshot();
  codex.disconnect();
  timeSync.disconnect();
  actions.disconnect();
  scriptsPage.reset();
  clipboardPage.reset();
}

// Only the visible action directory loads; shortcuts never depend on its cache.
void enterActionPage(uint32_t now) {
  if (!session.ready()) return;
  if (navigation.current() == adv::Module::kScripts) actions.enter(adv::ActionType::kScript, now);
  if (navigation.current() == adv::Module::kClipboard) actions.enter(adv::ActionType::kClipboard, now);
}

void handleConnection() {
  if (!ble.consumeConnectionChanged()) return;
  // A disconnect/reconnect between loop iterations can already look connected.
  // Invalidate the old request/cache before accepting the new transport generation.
  clearModuleSession();
  if (ble.connected()) session.onBleConnected();
  else {
    session.disconnect();
  }
  redrawRequested = true;
}

void handleMessages() {
  std::string raw;
  while (ble.takeMessage(raw)) {
    const auto decoded = codec.decode(raw);
    if (!decoded.ok) continue;
    if (router.route(decoded.message)) redrawRequested = true;
  }
}

void handleKey(const adv::KeyEvent& event, uint32_t now) {
  // Fn is fully consumed before Alt, then per-module mapping and page input.
  const auto routed = inputRouter.route(event, navigation.current(), settingsPage.textEditing(navigation.current()));
  const auto previous = navigation.current();
  if (routed.action == adv::InputAction::kNavigation) {
    navigation.handleGlobal(routed.event);
    if (navigation.current() != previous) {
      codexPage.invalidateTimeSnapshot();
      if (previous == adv::Module::kSettings) settingsPage.leave();
      codex.onPageChanged(navigation.current() == adv::Module::kCodex);
      enterActionPage(now);
      redrawRequested = true;
    }
    return;  // Switching pages preserves both directories and the shared execution.
  }
  if (routed.action == adv::InputAction::kShortcut) {
    actions.executeByKey(routed.event.character, now);
    redrawRequested = true;
    return;
  }
  // Device settings remain usable before hello and across BLE disconnections.
  if (settingsPage.handle(navigation.current(), routed, now)) {
    redrawRequested = true;
    return;
  }
  if (!session.ready()) return;
  if (routed.action == adv::InputAction::kConsumed || routed.action == adv::InputAction::kPageKey) return;
  if (navigation.current() == adv::Module::kCodex) {
    if (routed.action == adv::InputAction::kDirection) {
      if (routed.event.key == adv::Key::kUp) codexPage.scroll(-1, codex.state().windows().size());
      if (routed.event.key == adv::Key::kDown) codexPage.scroll(1, codex.state().windows().size());
    } else if (routed.action == adv::InputAction::kConfirm || routed.action == adv::InputAction::kCodexToggle) {
      codex.onKey(routed.event, now);
    }
    redrawRequested = true;
  } else if (navigation.current() == adv::Module::kScripts || navigation.current() == adv::Module::kClipboard) {
    const auto type = navigation.current() == adv::Module::kScripts ? adv::ActionType::kScript : adv::ActionType::kClipboard;
    if (routed.action == adv::InputAction::kConfirm) actions.confirm(type, now);
    if (routed.action == adv::InputAction::kDirection) {
      if (routed.event.key == adv::Key::kUp) actions.moveSelection(type, -1, now);
      if (routed.event.key == adv::Key::kDown) actions.moveSelection(type, 1, now);
    }
    redrawRequested = true;
  }
}

void handleKeyboard(uint32_t now) {
  keyboard.update();
  const bool wasVisible = screenPower.visible();
  const bool allowInput = screenPower.onKeyboard(keyboard.activity(), now);
  const auto events = keyboard.takePressedEvents();  // Always drain suppressed wake events.
  if (!wasVisible && screenPower.visible()) redrawRequested = true;
  if (allowInput) for (const auto& event : events) handleKey(event, now);
}

void draw(uint32_t now) {
  if (!screenPower.visible() || !redrawRequested) return;
  redrawRequested = false;
  shell.beginFrame(display, navigation.current(), battery.snapshot());
  if (navigation.current() == adv::Module::kSettings) settingsPage.render();
  else if (!session.ready()) shell.renderDisconnected(display);
  else if (navigation.current() == adv::Module::kCodex) {
    codexPage.render(display, codex.state(), now, codex.taskState());
  }
  else if (navigation.current() == adv::Module::kScripts) scriptsPage.render(display, actions.state(adv::ActionType::kScript));
  else if (navigation.current() == adv::Module::kClipboard) clipboardPage.render(display, actions.state(adv::ActionType::kClipboard));
  shell.renderFeedback(display, actions.execution().feedback);
  shell.endFrame(display);
  // Restore light only after the latest frame; never expose the stale off-screen frame.
  if (!backlightOn) { display.setBrightness(userBrightness); backlightOn = true; }
}
}

void setup() {
  Serial.begin(115200);
  display.begin();
  configStore.begin();
  // Apply persisted CPU frequency before starting either wireless service.
  settings.loadPower();
  Serial.printf("power cpu_mhz=%lu error=%d automatic_sleep=unsupported\n",
                static_cast<unsigned long>(settings.cpuFrequencyMhz()), settings.powerSaveFailed());
  settings.loadBrightness();
  keyboard.begin();
  ble.begin();
  const uint32_t now = clockSource.nowMs();
  screenPower.begin(now);
  timeSync.begin(now);
  codex.begin(now);
  // Registration owns the startup sample; keys and drawing never sample.
  scheduler.registerTask({adv::ScheduledTaskId::kBatterySample, 60000, true, true},
                        [](uint32_t) { if (battery.sample()) redrawRequested = true; }, now);
  // Long-lived display cadence belongs to the scheduler; drawing stays in loop().
  scheduler.registerTask({adv::ScheduledTaskId::kDisplayRefresh, 60000, true, false},
                        [](uint32_t tickNow) {
                          if (screenPower.visible() && session.ready() && navigation.current() == adv::Module::kCodex &&
                              codexPage.timeChanged(codex.state(), tickNow, codex.taskState())) redrawRequested = true;
                        }, now);
  const auto configResult = codexConfig.reload();
  Serial.printf("config startup=%s activeIntervalSeconds=%lu\n",
                adv::configStatusName(configResult.status),
                static_cast<unsigned long>(codex.taskState().intervalMs / 1000));
  settings.refreshCodex();
  settings.loadWifi();  // Read only: no connection or scan on startup.
  const auto inputResult = inputConfig.reload();
  Serial.printf("input.config startup=%s active=%s\n", adv::configStatusName(inputResult.status),
                adv::encodeInputConfig(inputConfig.active()).c_str());
  router.registerHandler(adv::protocol::kHelloAction, [](const adv::Message& message) {
    const bool wasReady = session.ready();
    const std::string previousComputer = session.computerId();
    if (session.acceptHello(message)) {
      // Identity changes invalidate every module even without a BLE disconnect.
      if (wasReady && previousComputer != session.computerId()) {
        clearModuleSession();
      }
      const uint32_t now = clockSource.nowMs();
      timeSync.onSessionReady(session.computerId(),
                              session.supports(adv::protocol::kTimeReadAction), now);
      codex.onSessionReady(session.supports(adv::protocol::kCodexUsageAction), now);
      // Background queries precede the visible directory; hello may arrive after navigation.
      actions.onSessionReady(session.computerId(), session.supports(adv::protocol::kActionsListAction),
                             session.supports(adv::protocol::kActionsExecuteAction),
                             session.supports(adv::protocol::kShortcutExecuteAction), session.supportedActionTypes());
      enterActionPage(now);
    } else {
      // A rejected renegotiation must not let a later hello revive the old computer's cache.
      clearModuleSession();
    }
  });
  router.registerHandler(adv::protocol::kTimeReadAction, [](const adv::Message& message) {
    timeSync.onMessage(message, clockSource.nowMs());
  });
  router.registerHandler(adv::protocol::kCodexUsageAction, [](const adv::Message& message) {
    codex.onMessage(message, clockSource.nowMs());
  });
  for (const auto* action : {adv::protocol::kActionsListAction, adv::protocol::kActionsExecuteAction,
                             adv::protocol::kShortcutExecuteAction}) {
    router.registerHandler(action, [](const adv::Message& message) {
      actions.onMessage(message, clockSource.nowMs());
    });
  }
}

void loop() {
  uint32_t now = clockSource.nowMs();
  // BLE callbacks only enqueue; protocol, state, input and drawing are serialized here.
  ble.poll(now);
  handleConnection();
  handleMessages();
  if (configCommands.poll([] { return Serial.available() ? Serial.read() : -1; })) {
    redrawRequested = true;
  }
  // Message handlers timestamp completed requests. Refresh now afterwards so
  // wrap-safe unsigned subtraction never compares an older loop time with a
  // newer completion time and mistakes the underflow for an expired interval.
  now = clockSource.nowMs();
  handleKeyboard(now);
  // Settings saves/platform starts can take time; sample again before any timeout.
  now = clockSource.nowMs();
  // One shared Wi-Fi service advances even off-page or without a BLE session.
  if (settings.tick(now)) redrawRequested = true;
  if (settingsPage.tick(clockSource.nowMs())) redrawRequested = true;
  now = clockSource.nowMs();
  const bool wasInFlight = codex.state().inFlight();
  const auto previousDirectory = actions.state(adv::ActionType::kScript).directory;
  const auto previousClipboard = actions.state(adv::ActionType::kClipboard).directory;
  const auto previousExecution = actions.execution().status;
  const bool hadFeedback = !actions.execution().feedback.empty();
  actions.tick(now);
  // Expiration must redraw once to restore the page footer even on an idle page.
  if (previousClipboard != actions.state(adv::ActionType::kClipboard).directory ||
      previousDirectory != actions.state(adv::ActionType::kScript).directory || previousExecution != actions.execution().status ||
      hadFeedback != !actions.execution().feedback.empty()) redrawRequested = true;
  timeSync.tick(now);
  codex.tick(now);
  scheduler.tick(now);
  ble.pollTransmit(clockSource.nowMs());
  if (codex.state().inFlight() != wasInFlight) redrawRequested = true;
  now = clockSource.nowMs();
  screenPower.tick(now);
  if (!screenPower.visible() && backlightOn) {
    display.setBrightness(0); backlightOn = false;
  }
  draw(now);
  delay(10);
}
#endif
