#include "application/time_sync/time_sync_controller.h"
#include <algorithm>

#include "core/protocol_constants.h"
#include "platform/monotonic_clock.h"

namespace adv {
namespace {
constexpr auto kTaskId = ScheduledTaskId::kSystemTimeSync;
constexpr uint32_t kSyncIntervalMs = 3600000;
constexpr uint32_t kRequestTimeoutMs = 15000;
constexpr uint32_t kRetryDelayMs = 3000;
constexpr unsigned kMaxAttempts = 3;
}

TimeSyncController::TimeSyncController(ScheduledTaskService& scheduler, ExecIdGenerator& ids,
    SystemTimeService& time, Send send, Cancel cancel)
    : scheduler_(scheduler), ids_(ids), time_(time), send_(std::move(send)), cancel_(std::move(cancel)) {}

bool TimeSyncController::begin(uint32_t now) {
  const bool registered = scheduler_.registerTask({kTaskId, kSyncIntervalMs, true, false},
      [this](uint32_t t) { startRound(t); }, now);
  if (registered) lastAgeTick_ = now;
  return registered;
}

bool TimeSyncController::enabled() const {
  ScheduledTaskSnapshot state{};
  return scheduler_.getTask(kTaskId, state) && state.enabled;
}

void TimeSyncController::updateAge(uint32_t now) {
  // Saturation latches expiry even when a long disconnection spans a later millis wrap.
  if (time_.hasSynced()) {
    successAge_ += std::min(elapsedMs(now, lastAgeTick_), kSyncIntervalMs - successAge_);
    if (successAge_ == kSyncIntervalMs) needsSync_ = true;
  }
  lastAgeTick_ = now;
}

void TimeSyncController::onSessionReady(const std::string& computer, bool supported, uint32_t now) {
  updateAge(now);
  const bool changed = !ready_ || computer_ != computer || (!supported_ && supported);
  if (computer_ != computer || (supported_ && !supported)) disconnect();
  ready_ = true;
  supported_ = supported;
  computer_ = computer;
  if (!time_.hasSynced() || successComputer_ != computer) needsSync_ = true;
  // A fresh same-computer reconnect must leave the successful hourly deadline intact.
  if (changed && needsSync_ && supported_ && enabled()) scheduler_.triggerNow(kTaskId, now);
}

void TimeSyncController::startRound(uint32_t now) {
  needsSync_ = true;
  if (!enabled() || !ready_ || !supported_ || !inFlight_.empty() || retryPending_) return;
  attempts_ = 0;
  attempt(now);
}

void TimeSyncController::attempt(uint32_t now) {
  // Rejected queue submissions also count, otherwise a full queue could retry forever.
  ++attempts_;
  std::string id;
  if (!ids_.next(id) || !send_(id, codec_.encodeTimeRequest(id), now)) {
    fail(now);
    return;
  }
  inFlight_ = id;
  startedAt_ = now;
}

void TimeSyncController::fail(uint32_t now) {
  if (!inFlight_.empty()) cancel_(inFlight_);
  inFlight_.clear();
  needsSync_ = true;
  failedAt_ = now;
  retryPending_ = attempts_ < kMaxAttempts;
}

void TimeSyncController::tick(uint32_t now) {
  updateAge(now);
  if (!inFlight_.empty() && elapsedMs(now, startedAt_) >= kRequestTimeoutMs) fail(now);
  if (!enabled()) {
    retryPending_ = false;
    return;
  }
  if (retryPending_ && ready_ && supported_ && elapsedMs(now, failedAt_) >= kRetryDelayMs) {
    retryPending_ = false;
    attempt(now);
  }
}

bool TimeSyncController::onMessage(const Message& message, uint32_t now) {
  if (message.event != MessageEvent::kResponse || message.actionId != protocol::kTimeReadAction ||
      inFlight_.empty() || message.execId != inFlight_) return false;
  if (message.resultCode != "OK" || !time_.synchronize(message.epochMilliseconds, message.utcOffsetMinutes, now)) {
    fail(now);
    return true;
  }
  inFlight_.clear();
  needsSync_ = false;
  retryPending_ = false;
  successComputer_ = computer_;
  successAge_ = 0;
  lastAgeTick_ = now;
  // Only successful UTC application establishes the next hourly deadline.
  scheduler_.rescheduleFromNow(kTaskId, now);
  return true;
}

void TimeSyncController::disconnect() {
  if (!inFlight_.empty()) {
    cancel_(inFlight_);
    needsSync_ = true;
  }
  inFlight_.clear();
  retryPending_ = false;
  ready_ = false;
  supported_ = false;
}

}  // namespace adv
