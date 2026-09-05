#include "application/codex/codex_controller.h"

#include "platform/monotonic_clock.h"

namespace adv {
namespace {
constexpr auto kTaskId = ScheduledTaskId::kCodexUsageRefresh;
constexpr uint32_t kDefaultIntervalMs = CodexConfig{}.refreshIntervalSeconds * 1000;
constexpr uint32_t kRequestTimeoutMs = 210000;
}

CodexController::CodexController(ScheduledTaskService& scheduler, ExecIdGenerator& ids,
                                 Send send, Cancel cancel)
    : scheduler_(scheduler), ids_(ids), send_(std::move(send)), cancel_(std::move(cancel)) {}

bool CodexController::begin(uint32_t now) {
  return scheduler_.registerTask({kTaskId, kDefaultIntervalMs, true, false},
                                 [this](uint32_t t) { request(t); }, now);
}

bool CodexController::applyConfig(const CodexConfig& config, uint32_t now) {
  ScheduledTaskSnapshot task{};
  if (!config.valid() || !scheduler_.getTask(kTaskId, task)) return false;
  const auto interval = config.refreshIntervalSeconds * 1000;
  return task.intervalMs == interval || scheduler_.updateInterval(kTaskId, interval, now);
}

ScheduledTaskSnapshot CodexController::taskState() const {
  ScheduledTaskSnapshot result{false, kDefaultIntervalMs};
  scheduler_.getTask(kTaskId, result);
  return result;
}

void CodexController::onSessionReady(bool supported, uint32_t now) {
  const bool first = !sessionReady_ || (!supported_ && supported);
  sessionReady_ = true;
  if (supported_ && !supported) {
    if (state_.inFlight()) cancel_(state_.inFlightExecId());
    state_.disconnect();
  }
  supported_ = supported;
  state_.onSessionReady();
  if (first && supported_ && taskState().enabled) scheduler_.triggerNow(kTaskId, now);
}

void CodexController::onKey(const KeyEvent& event, uint32_t now) {
  if (!sessionReady_ || !pageActive_ || event.key != Key::kEnter) return;
  if (event.fn) {
    scheduler_.setEnabled(kTaskId, !taskState().enabled, now);
  } else if (request(now) && taskState().enabled) {
    // A rejected submission must not postpone the next automatic attempt.
    scheduler_.rescheduleFromNow(kTaskId, now);
  }
}

bool CodexController::request(uint32_t now) {
  if (!sessionReady_ || !supported_ || state_.inFlight()) return false;
  std::string id;
  if (!ids_.next(id) || !send_(id, codec_.encodeCodexUsageRequest(id), now)) return false;
  state_.beginRequest(id);
  startedAt_ = now;
  return true;
}

void CodexController::tick(uint32_t now) {
  // Three desktop RPCs can each wait 60s; include queue/BLE overhead in the total.
  if (state_.inFlight() && elapsedMs(now, startedAt_) >= kRequestTimeoutMs) {
    cancel_(state_.inFlightExecId());
    state_.failRequest("TIMEOUT");
  }
}

bool CodexController::onMessage(const Message& message, uint32_t now) {
  return state_.applyResponse(message, now);
}

void CodexController::disconnect() {
  if (state_.inFlight()) cancel_(state_.inFlightExecId());
  sessionReady_ = false;
  supported_ = false;
  state_.disconnect();
}

}  // namespace adv
