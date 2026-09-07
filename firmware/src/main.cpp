#if defined(ARDUINO) || defined(ADV_SETTINGS_INTEGRATION_TEST)
#ifdef ARDUINO
#include <Arduino.h>
#endif

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

namespace {
adv::DisplayAdapter display;
adv::KeyboardAdapter keyboard;
adv::MonotonicClock clockSource;
adv::BleTransport ble;
adv::ConnectionSession session;
adv::NavigationService navigation;
adv::InputRouter inputRouter;
adv::AppShell shell;
adv::PlaceholderPage placeholder;
adv::CodexPage codexPage;
adv::ScriptsPage scriptsPage;
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
adv::ScriptsController scripts(execIds, enqueueRequest, cancelPending);
adv::PlatformConfigFileStore configStore;
adv::InputConfigService inputConfig(configStore, inputRouter);
adv::CodexConfigService codexConfig(configStore, codex, [] { return clockSource.nowMs(); });
adv::DisplayConfigService displayConfig(configStore);
adv::WifiConfigService wifiConfig(configStore);
adv::PlatformWifiAdapter wifiAdapter;
adv::WifiService wifi(wifiConfig, wifiAdapter, clockSource);
adv::SettingsController settings(displayConfig, codexConfig, codex, wifiConfig, wifi,
                                 [](uint8_t value) { display.setBrightness(value); });
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
  codex.disconnect();
  timeSync.disconnect();
  scripts.disconnect();
  scriptsPage.reset();
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
      if (previous == adv::Module::kSettings) settingsPage.leave();
      codex.onPageChanged(navigation.current() == adv::Module::kCodex);
      redrawRequested = true;
    }
    return;  // Switching pages preserves script requests, selection and feedback.
  }
  if (routed.action == adv::InputAction::kShortcut) {
    scripts.executeByKey(routed.event.character, now);
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
  } else if (navigation.current() == adv::Module::kScripts) {
    if (routed.action == adv::InputAction::kConfirm) scripts.confirm(now);
    if (routed.action == adv::InputAction::kDirection) {
      if (routed.event.key == adv::Key::kUp) scripts.moveSelection(-1, now);
      if (routed.event.key == adv::Key::kDown) scripts.moveSelection(1, now);
    }
    redrawRequested = true;
  }
}

void handleKeyboard(uint32_t now) {
  keyboard.update();
  for (const auto& event : keyboard.takePressedEvents()) handleKey(event, now);
}

void draw(uint32_t now) {
  if (!redrawRequested) return;
  redrawRequested = false;
  shell.beginFrame(display, navigation.current(), display.batteryLevel(),
                   session.ready());
  if (navigation.current() == adv::Module::kSettings) settingsPage.render();
  else if (!session.ready()) shell.renderDisconnected(display);
  else if (navigation.current() == adv::Module::kCodex) {
    codexPage.render(display, codex.state(), now, codex.taskState());
  }
  else if (navigation.current() == adv::Module::kScripts) scriptsPage.render(display, scripts.state());
  else placeholder.render(display, navigation.current());
  shell.renderFeedback(display, scripts.state().feedback);
  shell.endFrame(display);
}
}

void setup() {
  Serial.begin(115200);
  display.begin();
  configStore.begin();
  settings.loadBrightness();
  keyboard.begin();
  ble.begin();
  const uint32_t now = clockSource.nowMs();
  timeSync.begin(now);
  codex.begin(now);
  // Long-lived display cadence belongs to the scheduler; drawing stays in loop().
  scheduler.registerTask({adv::ScheduledTaskId::kDisplayRefresh, 60000, true, false},
                        [](uint32_t) { redrawRequested = true; }, now);
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
      // Preserve initial ordering: time, Codex, then directory. Alt needs no directory.
      scripts.onSessionReady(session.computerId(), session.supports(adv::protocol::kActionsListAction),
                             session.supports(adv::protocol::kScriptsExecuteAction),
                             session.supports(adv::protocol::kShortcutExecuteAction), now);
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
  for (const auto* action : {adv::protocol::kActionsListAction, adv::protocol::kScriptsExecuteAction,
                             adv::protocol::kShortcutExecuteAction}) {
    router.registerHandler(action, [](const adv::Message& message) {
      scripts.onMessage(message, clockSource.nowMs());
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
  const auto previousDirectory = scripts.state().directory;
  const auto previousExecution = scripts.state().execution;
  const bool hadFeedback = !scripts.state().feedback.empty();
  scripts.tick(now);
  // Expiration must redraw once to restore the page footer even on an idle page.
  if (previousDirectory != scripts.state().directory || previousExecution != scripts.state().execution ||
      hadFeedback != !scripts.state().feedback.empty()) redrawRequested = true;
  timeSync.tick(now);
  codex.tick(now);
  scheduler.tick(now);
  ble.pollTransmit(clockSource.nowMs());
  if (codex.state().inFlight() != wasInFlight) redrawRequested = true;
  now = clockSource.nowMs();
  draw(now);
  delay(10);
}
#endif
