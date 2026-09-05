#pragma once
#include <functional>
#include "core/exec_id_generator.h"
#include "core/scheduled_task_service.h"
#include "core/system_time_service.h"
#include "core/message_codec.h"

namespace adv {
class TimeSyncController {
 public:
  using Send = std::function<bool(const std::string&, const std::string&, uint32_t)>;
  using Cancel = std::function<void(const std::string&)>;
  TimeSyncController(ScheduledTaskService&, ExecIdGenerator&, SystemTimeService&, Send, Cancel);
  bool begin(uint32_t nowMs);
  void onSessionReady(const std::string& computerId, bool supported, uint32_t nowMs);
  void disconnect();
  void tick(uint32_t nowMs);
  bool onMessage(const Message&, uint32_t nowMs);
  const std::string& inFlightExecId() const { return inFlight_; }
 private:
  bool enabled() const;
  void startRound(uint32_t nowMs);
  void attempt(uint32_t nowMs);
  void fail(uint32_t nowMs);
  void updateAge(uint32_t nowMs);
  ScheduledTaskService& scheduler_;
  ExecIdGenerator& ids_;
  SystemTimeService& time_;
  Send send_;
  Cancel cancel_;
  MessageCodec codec_;
  std::string computer_, successComputer_, inFlight_;
  bool ready_{false}, supported_{false}, needsSync_{true}, retryPending_{false};
  uint32_t startedAt_{0}, failedAt_{0}, lastAgeTick_{0}, successAge_{0};
  unsigned attempts_{0};
};
}
