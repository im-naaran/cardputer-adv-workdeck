#include "application/settings/settings_page.h"
#include <algorithm>

namespace adv {
namespace {
const char* const fields[] = {"SSID", "用户名", "密码"};
std::string fieldValue(const WifiConfig& config, size_t field) {
  return field == 0 ? config.ssid : field == 1 ? config.username : config.password;
}
void eraseLast(std::string& text) {
  if (text.empty()) return;
  // Delete one Unicode character, including SSIDs obtained through scanning.
  size_t start = text.size() - 1;
  while (start && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80) --start;
  text.resize(start);
}
std::string intervalText(uint32_t seconds) {
  return seconds % 60 ? std::to_string(seconds) + "秒" : std::to_string(seconds / 60) + "分钟";
}
}
void SettingsPage::enter(SettingsScreen screen) {
  screen_ = screen; focus_ = 0;
  if (screen == SettingsScreen::kCodex) {
    controller_.refreshCodex();
    if (!minutesDirty_) {
      const auto seconds = controller_.intervalSeconds();
      minutes_ = seconds % 60 ? "" : std::to_string(seconds / 60);
    }
  }
  if (screen == SettingsScreen::kWifi) controller_.refreshWifi();
}
void SettingsPage::edit(size_t field) {
  parent_ = screen_; returnFocus_ = focus_; field_ = field;
  editor_ = parent_ == SettingsScreen::kCodex ? minutes_ : fieldValue(controller_.draft(), field);
  editMessage_.clear(); screen_ = SettingsScreen::kEditor;
}
void SettingsPage::view(size_t field) {
  parent_ = screen_; returnFocus_ = focus_; field_ = field; viewPage_ = 0;
  screen_ = SettingsScreen::kViewer;
}
void SettingsPage::back() {
  if (screen_ == SettingsScreen::kViewer) { screen_ = parent_; focus_ = returnFocus_; }
  else if (screen_ == SettingsScreen::kScan) { screen_ = SettingsScreen::kWifi; focus_ = 3; }
  else { screen_ = SettingsScreen::kHome; focus_ = homeFocus_; }
}
bool SettingsPage::handle(Module module, const RoutedInput& input) {
  if (module != Module::kSettings || input.action == InputAction::kConsumed ||
      input.action == InputAction::kNavigation || input.action == InputAction::kShortcut) return false;
  const auto key = input.event.key;
  if (screen_ == SettingsScreen::kEditor) {
    if (key == Key::kEnter || key == Key::kTab) {
      if (parent_ == SettingsScreen::kCodex) { minutes_ = editor_; minutesDirty_ = true; }
      else controller_.setField(field_, editor_);
      screen_ = parent_; focus_ = returnFocus_;
      if (key == Key::kTab) focus_ = parent_ == SettingsScreen::kWifi ? (field_ + 1) % 3 : 1;
    } else if (key == Key::kBackspace) { eraseLast(editor_); editMessage_.clear(); }
    else if (input.event.text >= 32 && input.event.text <= 126) {
      const size_t limit = parent_ == SettingsScreen::kCodex ? 16 : field_ == 0 ? 32 : 64;
      if (editor_.size() >= limit) editMessage_ = "已达字节上限，未添加";
      else { editor_ += input.event.text; editMessage_.clear(); }
    }
    return true;
  }
  if (screen_ == SettingsScreen::kViewer) {
    if (key == Key::kEnter || key == Key::kBackspace) back();
    else {
      const size_t count = std::max<size_t>(1, (wrap(viewText()).size() + 2) / 3);
      if (key == Key::kTab) viewPage_ = (viewPage_ + 1) % count;
      if (key == Key::kRight || key == Key::kDown) viewPage_ = std::min(viewPage_ + 1, count - 1);
      if ((key == Key::kLeft || key == Key::kUp) && viewPage_) --viewPage_;
    }
    return true;
  }
  if (key == Key::kBackspace) { back(); return true; }
  if (screen_ == SettingsScreen::kBrightness) {
    int level = controller_.brightnessLevel();
    if (key == Key::kLeft || key == Key::kUp) controller_.setBrightness(level - 1);
    if (key == Key::kRight || key == Key::kDown) controller_.setBrightness(level + 1);
    if (key == Key::kTab) controller_.setBrightness(level % 5 + 1);
    if (key == Key::kEnter) controller_.setBrightness(level);
    return true;
  }
  const auto count = rows().size();
  focus_ = std::min(focus_, count - 1);
  if (key == Key::kTab) focus_ = (focus_ + 1) % count;
  if (key == Key::kDown && focus_ + 1 < count) ++focus_;
  if (key == Key::kUp && focus_) --focus_;
  // A visible view action is also reachable with Tab when direction mapping is off.
  if (screen_ == SettingsScreen::kWifi && focus_ < 3 && key == Key::kRight) view(focus_);
  if (key != Key::kEnter) return true;
  if (screen_ == SettingsScreen::kHome) {
    homeFocus_ = focus_;
    enter(focus_ == 0 ? SettingsScreen::kBrightness : focus_ == 1 ? SettingsScreen::kWifi : SettingsScreen::kCodex);
  } else if (screen_ == SettingsScreen::kCodex) {
    if (focus_ == 0) edit(0);
    else if (focus_ == 1) { if (controller_.saveMinutes(minutes_)) minutesDirty_ = false; }
    else view(3);
  } else if (screen_ == SettingsScreen::kWifi) {
    if (focus_ < 3) edit(focus_);
    else if (focus_ == 3) { if (controller_.scan()) enter(SettingsScreen::kScan); }
    else if (focus_ == 4) controller_.saveWifi();
    else if (focus_ == 5) { controller_.test(); view(3); }
    else if (focus_ == 6) view(3);
    else if (focus_ < 10) view(focus_ - 7);
    else { controller_.retryClose(); view(3); }
  } else if (screen_ == SettingsScreen::kScan) {
    if (focus_ < controller_.scans().count) {
      controller_.selectNetwork(focus_); screen_ = SettingsScreen::kWifi; focus_ = 0;
    } else if (focus_ == controller_.scans().count) controller_.scan();
    else if (focus_ == controller_.scans().count + 1) back();
    else view(3);
  }
  return true;
}
std::vector<std::string> SettingsPage::rows() const {
  switch (screen_) {
    case SettingsScreen::kHome: return {"屏幕亮度 " + std::to_string(controller_.brightnessLevel()*20) + "%",
        "Wi-Fi " + controller_.wifiSummary(), "Codex自动刷新 " + intervalText(controller_.intervalSeconds())};
    case SettingsScreen::kCodex: return {"分钟：" + (minutes_.empty() ? "待输入" : minutes_), "保存", "查看保存及运行状态"};
    case SettingsScreen::kWifi: return {"SSID：" + controller_.draft().ssid, "用户名：" + controller_.draft().username,
        "密码：" + controller_.draft().password, "扫描网络", "保存", "测试连接", "查看操作及测试结果",
        "查看完整SSID", "查看完整用户名", "查看完整密码", "重试关闭Wi-Fi"};
    case SettingsScreen::kScan: {
      std::vector<std::string> result;
      for (size_t i = 0; i < controller_.scans().count; ++i) {
        const auto& network = controller_.scans().networks[i];
        result.push_back(std::to_string(network.rssi) + " " + network.ssid);
      }
      result.push_back("重新扫描"); result.push_back("返回手动配置"); result.push_back("查看扫描结果");
      return result;
    }
    default: return {""};
  }
}
std::string SettingsPage::viewText() const {
  if (parent_ == SettingsScreen::kCodex) {
    const auto& saved = controller_.savedCodex();
    return controller_.codexMessage() + "\n当前运行：" + intervalText(controller_.intervalSeconds()) +
        (saved.status == ConfigStatus::kOk ? "\n文件周期：" + intervalText(saved.config.refreshIntervalSeconds) : "");
  }
  if (field_ < 3) return std::string(fields[field_]) + "：\n" + fieldValue(controller_.draft(), field_);
  return controller_.wifiDetails();
}
std::vector<std::string> SettingsPage::wrap(const std::string& text) const {
  std::vector<std::string> lines;
  std::string line;
  // Wrap with the actual Chinese font metrics; never split a UTF-8 sequence.
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '\n') { lines.push_back(line); line.clear(); ++i; continue; }
    size_t end = i + 1;
    while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) ++end;
    const auto character = text.substr(i, end - i);
    if (!line.empty() && display_.textWidth(line + character, FontStyle::kChinese) > display_.width() - 16) {
      lines.push_back(line); line.clear();
    }
    line += character; i = end;
  }
  lines.push_back(line);
  return lines;
}
void SettingsPage::footer(const std::string& text) {
  const auto lines = wrap(text);
  for (size_t i = 0; i < lines.size() && i < 2; ++i)
    display_.drawText(lines[i], 8, 101 + i * 17, color::kMuted, FontStyle::kChinese);
}
void SettingsPage::render() {
  std::string title = "设置";
  if (screen_ == SettingsScreen::kBrightness) title = "亮度 Tab调整 Enter重试";
  if (screen_ == SettingsScreen::kCodex) title = "当前 " + intervalText(controller_.intervalSeconds());
  if (screen_ == SettingsScreen::kWifi) title = "Wi-Fi 明文配置";
  if (screen_ == SettingsScreen::kScan) title = "扫描网络 " + controller_.wifiSummary();
  if (screen_ == SettingsScreen::kEditor) title = parent_ == SettingsScreen::kCodex ? "编辑分钟（1至60）" : std::string("编辑") + fields[field_];
  if (screen_ == SettingsScreen::kViewer) title = "完整查看";
  display_.drawText(display_.fitText(title, display_.width()-16), 8, 25, color::kAccent, FontStyle::kChinese);
  if (screen_ == SettingsScreen::kBrightness) {
    for (int i = 1; i <= 5; ++i) {
      const bool selected = controller_.brightnessLevel() == i;
      display_.fillRoundRect(8+(i-1)*45, 51, 42, 30, 4, selected ? color::kAccent : color::kSurface);
      display_.drawText(std::to_string(i*20)+"%", 11+(i-1)*45, 58, selected ? color::kAccentText : color::kText, FontStyle::kSmall);
    }
    footer(controller_.displayMessage()); return;
  }
  if (screen_ == SettingsScreen::kEditor || screen_ == SettingsScreen::kViewer) {
    const auto lines = wrap(screen_ == SettingsScreen::kEditor ? editor_ + "_" : viewText());
    const size_t count = std::max<size_t>(1, (lines.size()+2)/3);
    viewPage_ = std::min(viewPage_, count-1);
    const size_t start = screen_ == SettingsScreen::kEditor ? (lines.size() > 3 ? lines.size()-3 : 0) : viewPage_*3;
    for (size_t i = start; i < lines.size() && i < start+3; ++i)
      display_.drawText(lines[i], 8, 45+(i-start)*18, color::kText, FontStyle::kChinese);
    if (screen_ == SettingsScreen::kEditor) footer(editMessage_.empty() ? "Enter完成 Tab下一项" : editMessage_);
    else footer(std::to_string(viewPage_+1)+"/"+std::to_string(count)+" Tab翻页 Enter返回");
    return;
  }
  const auto items = rows(); focus_ = std::min(focus_, items.size()-1);
  const size_t start = focus_ / 3 * 3;
  for (size_t i = start; i < items.size() && i < start+3; ++i) {
    const int y = 44+(i-start)*18;
    if (i == focus_) display_.fillRoundRect(4, y, display_.width()-8, 18, 3, color::kSurface);
    display_.drawText(display_.fitText(items[i], display_.width()-16), 8, y,
                      i == focus_ ? color::kAccent : color::kText, FontStyle::kChinese);
  }
  if (screen_ == SettingsScreen::kWifi) {
    footer(focus_ < 3 ? "Enter编辑 右键完整查看\nTab下一项 退格返回" : controller_.wifiMessage());
  } else if (screen_ == SettingsScreen::kCodex) footer(controller_.codexMessage());
  else if (screen_ == SettingsScreen::kScan) footer(controller_.scans().truncated ? "仅显示较强32项，详见结果" : "Enter选择 Tab下一项\n退格返回，可手动输入");
  else footer("Tab选择 Enter进入\n退格返回");
}
}  // namespace adv
