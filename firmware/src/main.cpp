#ifdef ARDUINO
#include <Arduino.h>

#include "application/app_shell.h"
#include "application/codex/codex_controller.h"
#include "application/codex/codex_page.h"
#include "application/codex/codex_config_commands.h"
#include "application/placeholder_page.h"
#include "application/time_sync/time_sync_controller.h"
#include "core/connection_session.h"
#include "core/message_codec.h"
#include "core/message_router.h"
#include "core/navigation_service.h"
#include "core/protocol_constants.h"
#include "platform/ble_transport.h"
#include "platform/display_adapter.h"
#include "platform/keyboard_adapter.h"
#include "platform/monotonic_clock.h"

namespace {
adv::DisplayAdapter display;
adv::KeyboardAdapter keyboard;
adv::MonotonicClock clockSource;
adv::BleTransport ble;
adv::ConnectionSession session;
adv::NavigationService navigation;
adv::AppShell shell;
adv::PlaceholderPage placeholder;
adv::CodexPage codexPage;
adv::MessageCodec codec;
adv::MessageRouter router;
adv::ScheduledTaskService scheduler;
adv::ExecIdGenerator execIds;
adv::PlatformSystemClock systemClock;
adv::SystemTimeService systemTime(systemClock);
// Both controllers submit through the same bounded BLE queue.
bool enqueueRequest(const std::string& id, const std::string& jsonl, uint32_t now) {
  return ble.enqueueRequest(id, jsonl, now);
}

void cancelPending(const std::string& id) {
  ble.cancelPending(id);
}

adv::TimeSyncController timeSync(scheduler, execIds, systemTime, enqueueRequest, cancelPending);
adv::CodexController codex(scheduler, execIds, enqueueRequest, cancelPending);
adv::PlatformConfigFileStore configStore;
adv::CodexConfigService codexConfig(configStore, codex, [] { return clockSource.nowMs(); });

void printTimeDiagnostic() {
  int64_t utc = 0;
  const bool available = systemTime.currentUtcMilliseconds(utc);
  Serial.printf("time synced=%d utc_ms=%lld offset_min=%d last_sync_ms=%lu now_ms=%lu\n",
                available, static_cast<long long>(utc), systemTime.utcOffsetMinutes(),
                static_cast<unsigned long>(systemTime.lastSuccessfulSyncMs()),
                static_cast<unsigned long>(clockSource.nowMs()));
}

adv::CodexConfigCommands configCommands(codexConfig, codex,
    [](const std::string& line) { Serial.println(line.c_str()); }, printTimeDiagnostic);
bool redrawRequested = true;

void handleConnection() {
  if (!ble.consumeConnectionChanged()) return;
  // A disconnect/reconnect between loop iterations can already look connected.
  // Invalidate the old request/cache before accepting the new transport generation.
  codex.disconnect();
  timeSync.disconnect();
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

void handleKeyboard(uint32_t now) {
  keyboard.update();
  for (const auto& event : keyboard.takePressedEvents()) {
    const adv::Module previous = navigation.current();
    // Global Fn combinations always win over module-specific input.
    if (navigation.handleGlobal(event)) {
      if (navigation.current() != previous) {
        codex.onPageChanged(navigation.current() == adv::Module::kCodex);
        redrawRequested = true;
      }
      continue;
    }
    if (!session.ready()) continue;
    if (navigation.current() == adv::Module::kCodex) {
      const size_t previousOffset = codexPage.scrollOffset();
      const bool wasInFlight = codex.state().inFlight();
      const bool wasAutomatic = codex.taskState().enabled;
      if (event.key == adv::Key::kUp) {
        codexPage.scroll(-1, codex.state().windows().size());
      } else if (event.key == adv::Key::kDown) {
        codexPage.scroll(1, codex.state().windows().size());
      } else {
        codex.onKey(event, now);
      }
      if (codexPage.scrollOffset() != previousOffset ||
          codex.state().inFlight() != wasInFlight || codex.taskState().enabled != wasAutomatic) {
        redrawRequested = true;
      }
    }
  }
}

void draw(uint32_t now) {
  if (!redrawRequested) return;
  redrawRequested = false;
  shell.beginFrame(display, navigation.current(), display.batteryLevel(),
                   session.ready());
  if (!session.ready()) shell.renderDisconnected(display);
  else if (navigation.current() == adv::Module::kCodex) {
    codexPage.render(display, codex.state(), now, codex.taskState());
  }
  else placeholder.render(display, navigation.current());
  shell.endFrame(display);
}
}

void setup() {
  Serial.begin(115200);
  display.begin();
  keyboard.begin();
  ble.begin();
  configStore.begin();
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
  router.registerHandler(adv::protocol::kHelloAction, [](const adv::Message& message) {
    const bool wasReady = session.ready();
    const std::string previousComputer = session.computerId();
    if (session.acceptHello(message)) {
      // Identity changes invalidate both actions even without a BLE disconnect.
      if (wasReady && previousComputer != session.computerId()) {
        timeSync.disconnect();
        codex.disconnect();
      }
      const uint32_t now = clockSource.nowMs();
      timeSync.onSessionReady(session.computerId(),
                              session.supports(adv::protocol::kTimeReadAction), now);
      codex.onSessionReady(session.supports(adv::protocol::kCodexUsageAction), now);
    } else {
      // A rejected renegotiation must not let a later hello revive the old computer's cache.
      codex.disconnect();
      timeSync.disconnect();
    }
  });
  router.registerHandler(adv::protocol::kTimeReadAction, [](const adv::Message& message) {
    timeSync.onMessage(message, clockSource.nowMs());
  });
  router.registerHandler(adv::protocol::kCodexUsageAction, [](const adv::Message& message) {
    codex.onMessage(message, clockSource.nowMs());
  });
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
  const bool wasInFlight = codex.state().inFlight();
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
