#pragma once
#include <functional>
#include "application/codex/codex_config.h"
#include "application/codex/codex_usage_state.h"
#include "core/exec_id_generator.h"
#include "core/scheduled_task_service.h"
#include "platform/keyboard_adapter.h"

namespace adv {
class CodexController {
 public:
  using Send = std::function<bool(const std::string&, const std::string&, uint32_t)>;
  using Cancel = std::function<void(const std::string&)>;
  CodexController(ScheduledTaskService& scheduler, ExecIdGenerator& ids, Send send, Cancel cancel);
  bool begin(uint32_t nowMs);
  bool applyConfig(const CodexConfig&, uint32_t nowMs);
  void onSessionReady(bool supported, uint32_t nowMs);
  void onPageChanged(bool active) { pageActive_ = active; }
  void onKey(const KeyEvent&, uint32_t nowMs);
  void tick(uint32_t nowMs);
  bool onMessage(const Message&, uint32_t nowMs);
  void disconnect();
  ScheduledTaskSnapshot taskState() const;
  const CodexUsageState& state() const { return state_; }
 private:
  bool request(uint32_t nowMs);
  ScheduledTaskService& scheduler_;
  ExecIdGenerator& ids_;
  Send send_;
  Cancel cancel_;
  CodexUsageState state_;
  MessageCodec codec_;
  bool pageActive_{true}, sessionReady_{false}, supported_{false};
  uint32_t startedAt_{0};
};
}
