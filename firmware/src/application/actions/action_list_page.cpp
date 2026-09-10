#include "application/actions/action_list_page.h"
#include <algorithm>
namespace adv {
ActionListPageView ActionListPage::makeView(DisplayAdapter& display, const ActionDirectoryState& state) {
  ActionListPageView view;
  const std::string label = type_ == ActionType::kScript ? "脚本" : "剪贴板";
  switch (state.directory) {
    case ActionDirectoryStatus::kUnsupported:
      view.message = label + "目录不可用"; view.hint = "请检查桌面端能力"; break;
    case ActionDirectoryStatus::kNotLoaded:
    case ActionDirectoryStatus::kLoading:
      view.message = "正在加载" + label; view.hint = "请稍候"; break;
    case ActionDirectoryStatus::kFailed:
      view.message = state.directoryError.empty() ? label + "加载失败" : state.directoryError;
      view.hint = "按 Enter 重试";
      break;
    case ActionDirectoryStatus::kReady:
      if (state.entries.empty()) { view.message = "暂无启用" + label; view.hint = "请检查电脑配置"; }
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
  view.footer = std::to_string(state.offset + selected + 1) + "/" + std::to_string(state.total) + (type_ == ActionType::kScript ? "  Enter 执行" : "  Enter 粘贴");
  return view;
}
void ActionListPage::render(DisplayAdapter& display, const ActionDirectoryState& state) {
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
