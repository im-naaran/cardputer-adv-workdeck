#include "application/codex/codex_page.h"

#include <algorithm>
#include <cstdio>

#include "application/app_shell.h"
#include "platform/monotonic_clock.h"

namespace adv {
namespace {
std::array<std::string, 3> visibleTimes(const CodexPageView& view) {
  std::array<std::string, 3> result{};
  result[0] = view.refreshing ? "刷新中" : view.footer;
  if (view.centerMessage.empty()) {
    for (size_t i = 0; i < 2 && view.scrollOffset + i < view.rows.size(); ++i)
      result[i + 1] = view.rows[view.scrollOffset + i].resetText;
  }
  return result;
}
bool displaySafe(const std::string& text) {
  // Reject controls and four-byte Unicode (typically emoji) absent from efontCN_16.
  for (size_t i = 0; i < text.size();) {
    const unsigned char byte = static_cast<unsigned char>(text[i]);
    if (byte < 0x20 || byte == 0x7f) return false;
    size_t count = byte < 0x80 ? 1 : ((byte & 0xE0) == 0xC0 ? 2 : ((byte & 0xF0) == 0xE0 ? 3 : 0));
    if (count == 0 || i + count > text.size()) return false;
    for (size_t j = 1; j < count; ++j) {
      if ((static_cast<unsigned char>(text[i + j]) & 0xC0) != 0x80) return false;
    }
    i += count;
  }
  return !text.empty();
}

std::string fitText(const std::string& text, size_t maxWidthUnits) {
  size_t units = 0;
  size_t index = 0;
  while (index < text.size()) {
    const unsigned char byte = static_cast<unsigned char>(text[index]);
    const size_t count = byte < 0x80 ? 1 : ((byte & 0xE0) == 0xC0 ? 2 : 3);
    const size_t nextUnits = units + (count == 1 ? 1 : 2);
    if (nextUnits > maxWidthUnits - 2) return text.substr(0, index) + "..";
    units = nextUnits;
    index += count;
  }
  return text;
}

std::string fitTitle(const std::string& text) { return fitText(text, 18); }

std::string durationLabel(int minutes) {
  if (minutes <= 0) return "额度窗口";
  if (minutes % 1440 == 0) return std::to_string(minutes / 1440) + "天额度";
  if (minutes % 60 == 0) return std::to_string(minutes / 60) + "小时额度";
  return std::to_string(minutes) + "分钟额度";
}
std::string windowTitle(const UsageWindow& window) {
  std::string title;
  if (displaySafe(window.limitName)) title = window.limitName;
  else if (window.hasWindowDuration) title = durationLabel(window.windowDurationMins);
  else if (displaySafe(window.limitId)) title = window.limitId;
  else title = window.windowKind == "secondary" ? "次要额度" : "主要额度";
  return fitTitle(title);
}
std::string errorLabel(const std::string& code) {
  if (code == "TIMEOUT") return "查询超时";
  if (code == "BUSY") return "查询繁忙";
  if (code == "CODEX_UNAVAILABLE") return "Codex不可用";
  if (code == "INVALID_RESPONSE") return "响应异常";
  return "查询失败";
}
}  // namespace

int clampPercent(int value) { return std::max(0, std::min(100, value)); }

int remainingPercent(int usedPercent) { return clampPercent(100 - usedPercent); }

std::string formatRelativeAge(uint32_t nowMs, uint32_t receivedAtMs) {
  const uint32_t minutes = elapsedMs(nowMs, receivedAtMs) / 60000u;
  if (minutes == 0) return "刚刚更新";
  if (minutes < 60) return std::to_string(minutes) + "分钟前";
  const uint32_t hours = minutes / 60;
  if (hours < 24) return std::to_string(hours) + "小时前";
  return std::to_string(hours / 24) + "天前";
}

std::string formatResetRemaining(const UsageWindow& window, int64_t fetchedAtEpochSeconds,
                                 uint32_t nowMs, uint32_t receivedAtMs) {
  if (!window.hasResetEpoch || fetchedAtEpochSeconds <= 0) return "重置 --";
  // Epoch is anchored only for this boot; elapsed display uses the wrap-safe clock.
  const int64_t estimatedNow = fetchedAtEpochSeconds + elapsedMs(nowMs, receivedAtMs) / 1000u;
  const int64_t seconds = window.resetsAtEpochSeconds - estimatedNow;
  if (seconds <= 0) return "即将重置";
  const int64_t minutes = (seconds + 59) / 60;
  char text[40];
  if (minutes >= 1440) {
    std::snprintf(text, sizeof(text), "%lld天%lld小时后重置",
                  static_cast<long long>(minutes / 1440),
                  static_cast<long long>((minutes % 1440) / 60));
  } else if (minutes >= 60) {
    std::snprintf(text, sizeof(text), "%lld小时%lld分后重置",
                  static_cast<long long>(minutes / 60),
                  static_cast<long long>(minutes % 60));
  } else {
    std::snprintf(text, sizeof(text), "%lld分钟后重置", static_cast<long long>(minutes));
  }
  return text;
}

void CodexPage::scroll(int delta, size_t rowCount) {
  const size_t maxOffset = rowCount > 2 ? rowCount - 2 : 0;
  if (delta < 0) scrollOffset_ = scrollOffset_ == 0 ? 0 : scrollOffset_ - 1;
  else if (delta > 0) scrollOffset_ = std::min(maxOffset, scrollOffset_ + 1);
}

CodexPageView CodexPage::makeView(const CodexUsageState& state, uint32_t nowMs, ScheduledTaskSnapshot task) const {
  CodexPageView view;
  const uint32_t seconds = task.intervalMs / 1000;
  const std::string period = seconds % 3600 == 0 ? std::to_string(seconds/3600) + "h"
      : seconds % 60 == 0 ? std::to_string(seconds/60) + "m" : std::to_string(seconds) + "s";
  view.automaticLabel = task.enabled ? "自动 " + period : "自动已关";
  view.refreshing = state.inFlight();
  const auto status = state.status();
  if (status == UsageStatus::kDisconnected) {
    view.centerMessage = "等待电脑连接";
    view.centerHint = "请启动桌面端服务";
  } else if (status == UsageStatus::kReadyEmpty) {
    view.centerMessage = "暂无用量数据";
    view.centerHint = "按 Enter 立即查询";
  } else if (status == UsageStatus::kLoading) {
    view.centerMessage = "正在查询用量";
    view.centerHint = "请稍候";
  } else if (status == UsageStatus::kNotLoggedIn) {
    view.centerMessage = "请先登录 Codex";
    view.centerHint = "登录后按 Enter 重试";
  } else if (status == UsageStatus::kError) {
    view.centerMessage = errorLabel(state.lastErrorCode());
    view.centerHint = "按 Enter 重新查询";
  }
  for (const auto& window : state.windows()) {
    view.rows.push_back({windowTitle(window), remainingPercent(window.usedPercent),
                         formatResetRemaining(window, state.fetchedAtEpochSeconds(), nowMs,
                                              state.receivedAtMs())});
  }
  const size_t maxOffset = view.rows.size() > 2 ? view.rows.size() - 2 : 0;
  view.scrollOffset = std::min(scrollOffset_, maxOffset);
  if (state.hasData()) {
    view.footer = formatRelativeAge(nowMs, state.receivedAtMs());
    if (status == UsageStatus::kStale) view.footer += " · " + errorLabel(state.lastErrorCode());
  }
  return view;
}

bool CodexPage::timeChanged(const CodexUsageState& state, uint32_t nowMs,
                            ScheduledTaskSnapshot task) const {
  return !timeSnapshotValid_ || visibleTimes(makeView(state, nowMs, task)) != timeSnapshot_;
}

void CodexPage::render(DisplayAdapter& display, const CodexUsageState& state,
                       uint32_t nowMs, ScheduledTaskSnapshot task) {
  const CodexPageView view = makeView(state, nowMs, task);
  // Cache only on actual drawing, never on an off-screen periodic check.
  timeSnapshot_ = visibleTimes(view);
  timeSnapshotValid_ = true;
  // Reserve task status before any center-state early return; truncate only secondary detail.
  display.drawText(view.automaticLabel, 6, 118, color::kMuted, FontStyle::kChinese);
  const int detailX = 12 + display.textWidth(view.automaticLabel, FontStyle::kChinese);
  std::string detail = view.refreshing ? "刷新中" : view.footer;
  while (!detail.empty() && display.textWidth(detail, FontStyle::kChinese) > 234-detailX) {
    size_t last = detail.size()-1;
    while (last > 0 && (static_cast<unsigned char>(detail[last]) & 0xc0) == 0x80) --last;
    detail.resize(last);
  }
  display.drawText(detail, detailX, 118, color::kMuted, FontStyle::kChinese);
  if (!view.centerMessage.empty()) {
    const bool isError = state.status() == UsageStatus::kError;
    const bool needsLogin = state.status() == UsageStatus::kNotLoggedIn;
    const uint16_t accent = isError ? color::kRed
                                    : (needsLogin ? color::kAmber : color::kAccent);
    const uint16_t symbol = isError ? color::kWhite : color::kAccentText;
    display.fillRoundRect(12, 32, 216, 82, 8, color::kSurface);
    display.drawRoundRect(12, 32, 216, 82, 8, color::kBorder);
    display.fillCircle(120, 47, 8, accent);
    if (state.status() == UsageStatus::kLoading) {
      display.drawLine(120, 40, 120, 44, symbol);
      display.drawLine(120, 50, 124, 50, symbol);
    } else if (isError) {
      display.drawLine(120, 42, 120, 48, symbol);
      display.fillCircle(120, 51, 1, symbol);
    } else {
      display.drawLine(116, 47, 119, 50, symbol);
      display.drawLine(119, 50, 125, 43, symbol);
    }
    display.drawText(view.centerMessage,
                     (display.width() - display.textWidth(view.centerMessage,
                                                          FontStyle::kChinese)) / 2,
                     62, color::kText, FontStyle::kChinese);
    display.drawText(view.centerHint,
                     (display.width() - display.textWidth(view.centerHint,
                                                          FontStyle::kChinese)) / 2,
                     87, color::kMuted, FontStyle::kChinese);
    return;
  }
  for (size_t visible = 0; visible < 2; ++visible) {
    const size_t index = view.scrollOffset + visible;
    if (index >= view.rows.size()) break;
    const auto& row = view.rows[index];
    const int y = 26 + static_cast<int>(visible) * 44;
    display.fillRoundRect(5, y, 230, 42, 7, color::kSurface);
    display.drawRoundRect(5, y, 230, 42, 7, color::kBorder);
    display.drawText(row.title, 12, y + 2, color::kText, FontStyle::kChinese);
    const std::string percent = std::to_string(row.percent) + "%";
    display.drawText(percent, 226 - display.textWidth(percent), y + 3,
                     color::kAccent, FontStyle::kBody);
    display.fillRoundRect(12, y + 20, 216, 6, 3, color::kTrack);
    const int progressWidth = 216 * row.percent / 100;
    const uint16_t progressColor = row.percent <= 10 ? color::kRed
                                    : row.percent <= 30 ? color::kAmber
                                                       : color::kAccent;
    if (progressWidth > 0) {
      display.fillRoundRect(12, y + 20, progressWidth, 6,
                            std::min(3, progressWidth / 2), progressColor);
    }
    display.drawText(row.resetText, 12, y + 26, color::kMuted,
                     FontStyle::kChinese);
  }
  if (view.rows.size() > 2) {
    display.fillRoundRect(237, 29, 2, 79, 1, color::kBorder);
    const int thumbHeight = std::max(14, 79 * 2 / static_cast<int>(view.rows.size()));
    const int travel = 79 - thumbHeight;
    const int maxOffset = static_cast<int>(view.rows.size()) - 2;
    const int thumbY = 29 + (maxOffset > 0 ? travel * static_cast<int>(view.scrollOffset) /
                                              maxOffset : 0);
    display.fillRoundRect(237, thumbY, 2, thumbHeight, 1, color::kAccent);
  }
}

}  // namespace adv
