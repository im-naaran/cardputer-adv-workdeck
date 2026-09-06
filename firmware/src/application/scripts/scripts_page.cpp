#include "application/scripts/scripts_page.h"
#include <algorithm>
namespace adv {
ScriptsPageView ScriptsPage::makeView(DisplayAdapter& display, const ScriptsState& state) {
  ScriptsPageView view;
  switch (state.directory) {
    case ScriptsDirectoryStatus::kUnsupported:
      view.message = "脚本目录不可用"; view.hint = "请检查桌面端能力"; break;
    case ScriptsDirectoryStatus::kLoading:
      view.message = "正在加载脚本"; view.hint = "请稍候"; break;
    case ScriptsDirectoryStatus::kFailed:
      view.message = state.directoryError.empty() ? "脚本加载失败" : state.directoryError;
      view.hint = "按 Enter 重试";
      break;
    case ScriptsDirectoryStatus::kReady:
      if (state.entries.empty()) { view.message = "暂无启用脚本"; view.hint = "请检查电脑配置"; }
      break;
  }
  // During paging the old cache is retained only by the controller. Hiding the
  // rows here prevents the failed/loading target from looking like an executable page.
  if (!view.message.empty()) return view;
  constexpr size_t kVisibleRows = 4;
  const size_t selected = std::min(state.selected, state.entries.size() - 1);
  if (selected < scrollOffset_) scrollOffset_ = selected;
  if (selected >= scrollOffset_ + kVisibleRows) scrollOffset_ = selected - kVisibleRows + 1;
  scrollOffset_ = std::min(scrollOffset_, state.entries.size() > kVisibleRows ? state.entries.size() - kVisibleRows : 0);
  view.scrollOffset = scrollOffset_;
  for (size_t i = scrollOffset_; i < state.entries.size() && i < scrollOffset_ + kVisibleRows; ++i) {
    const auto& entry = state.entries[i];
    std::string shortcut;
    if (!entry.effectiveKey.empty()) shortcut = std::string("Alt+") + static_cast<char>(entry.effectiveKey[0] - 'a' + 'A');
    const int space = shortcut.empty() ? 214 : 208 - display.textWidth(shortcut, FontStyle::kSmall);
    view.rows.push_back({display.fitText(entry.name, space, FontStyle::kChinese), shortcut, i == selected});
  }
  view.footer = std::to_string(state.offset + selected + 1) + "/" + std::to_string(state.total) + "  Enter 执行";
  return view;
}
void ScriptsPage::render(DisplayAdapter& display, const ScriptsState& state) {
  const auto view = makeView(display, state);
  if (!view.message.empty()) {
    display.fillRoundRect(12, 32, 216, 82, 8, color::kSurface);
    display.drawRoundRect(12, 32, 216, 82, 8, color::kBorder);
    display.drawText(view.message, (display.width()-display.textWidth(view.message, FontStyle::kChinese))/2,
                     52, color::kText, FontStyle::kChinese);
    display.drawText(view.hint, (display.width()-display.textWidth(view.hint, FontStyle::kChinese))/2,
                     80, color::kMuted, FontStyle::kChinese);
    return;
  }
  for (size_t i = 0; i < view.rows.size(); ++i) {
    const auto& row = view.rows[i];
    const int y = 27 + static_cast<int>(i) * 21;
    const auto background = row.selected ? color::kAccent : color::kSurface;
    const auto foreground = row.selected ? color::kAccentText : color::kText;
    display.fillRoundRect(5, y, 230, 19, 4, background);
    display.drawText(row.title, 11, y+1, foreground, FontStyle::kChinese);
    display.drawText(row.shortcut, 228-display.textWidth(row.shortcut, FontStyle::kSmall), y+6,
                     foreground, FontStyle::kSmall);
  }
  display.drawText(display.fitText(view.footer, 228, FontStyle::kChinese), 6, 118, color::kMuted, FontStyle::kChinese);
}
}  // namespace adv
