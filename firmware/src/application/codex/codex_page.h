#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "application/codex/codex_usage_state.h"
#include "platform/display_adapter.h"
#include "core/scheduled_task_service.h"

namespace adv {

struct UsageRowView { std::string title; int percent{0}; std::string resetText; };
struct CodexPageView {
  std::vector<UsageRowView> rows;
  std::string centerMessage;
  std::string centerHint;
  std::string footer;
  std::string automaticLabel;
  bool refreshing{false};
  size_t scrollOffset{0};
};

int clampPercent(int value);
int remainingPercent(int usedPercent);
std::string formatRelativeAge(uint32_t nowMs, uint32_t receivedAtMs);
std::string formatResetRemaining(const UsageWindow& window, int64_t fetchedAtEpochSeconds,
                                 uint32_t nowMs, uint32_t receivedAtMs);

class CodexPage {
 public:
  void scroll(int delta, size_t rowCount);
  CodexPageView makeView(const CodexUsageState& state, uint32_t nowMs,
                        ScheduledTaskSnapshot task) const;
  bool timeChanged(const CodexUsageState& state, uint32_t nowMs, ScheduledTaskSnapshot task) const;
  void invalidateTimeSnapshot() { timeSnapshotValid_ = false; }
  void render(DisplayAdapter& display, const CodexUsageState& state, uint32_t nowMs,
              ScheduledTaskSnapshot task);
  size_t scrollOffset() const { return scrollOffset_; }
 private:
  size_t scrollOffset_{0};
  std::array<std::string, 3> timeSnapshot_{};
  bool timeSnapshotValid_{false};
};

}  // namespace adv
