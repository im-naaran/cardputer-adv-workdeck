#include "application/settings/settings_page.h"
#include <algorithm>

namespace adv {
namespace {
constexpr uint32_t screenTimeouts[] = {0, 60, 300, 600, 1800};
std::string screenTimeoutText(uint32_t seconds) {
  return seconds ? std::to_string(seconds / 60) + "分钟" : "永不";
}
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
    if (!minutesDirty_) {
      controller_.refreshCodex();
      const auto seconds = controller_.intervalSeconds();
      minutes_ = seconds % 60 ? "" : std::to_string(seconds / 60);
    }
  }
  if (screen == SettingsScreen::kWifi) controller_.refreshWifi();
}
void SettingsPage::edit(size_t field) {
  field_ = field; editor_ = fieldValue(controller_.draft(), field);
  editMessage_.clear(); editing_ = true;
}
void SettingsPage::view() {
  parent_ = screen_; returnFocus_ = focus_; viewPage_ = 0;
  screen_ = SettingsScreen::kViewer;
}
void SettingsPage::feedback(const std::string& text) {
  feedback_ = text; feedbackAt_ = now_;
}
void SettingsPage::savePending() {
  saveScheduled_ = false;
  if (!minutesDirty_) return;
  minutesDirty_ = !controller_.saveMinutes(minutes_);
  feedback(controller_.codexMessage());
}
void SettingsPage::leave() { savePending(); }
bool SettingsPage::tick(uint32_t now) {
  now_ = now;
  bool changed = false;
  if (saveScheduled_ && uint32_t(now - changedAt_) >= 600) {
    savePending(); changed = true;
  }
  if (!feedback_.empty() && uint32_t(now - feedbackAt_) >= 2500) {
    feedback_.clear(); changed = true;
  }
  return changed;
}
void SettingsPage::back() {
  if (screen_ == SettingsScreen::kCodex) savePending();
  if (screen_ == SettingsScreen::kViewer) { screen_ = parent_; focus_ = returnFocus_; }
  else if (screen_ == SettingsScreen::kScan) { screen_ = SettingsScreen::kWifi; focus_ = 3; }
  else { screen_ = SettingsScreen::kHome; focus_ = homeFocus_; }
  feedback_.clear();
}
bool SettingsPage::handle(Module module, const RoutedInput& input, uint32_t now) {
  now_ = now;
  if (module != Module::kSettings || input.action == InputAction::kConsumed ||
      input.action == InputAction::kNavigation || input.action == InputAction::kShortcut) return false;
  const auto key = input.event.key;
  if (editing_) {
    if (key == Key::kEnter || key == Key::kTab) {
      controller_.setField(field_, editor_); editing_ = false;
      if (key == Key::kTab) { focus_ = (field_ + 1) % 3; edit(focus_); }
    } else if (key == Key::kBackspace) { eraseLast(editor_); editMessage_.clear(); }
    else if (input.event.text >= 32 && input.event.text <= 126) {
      const size_t limit = field_ == 0 ? 32 : 64;
      if (editor_.size() >= limit) editMessage_ = "已达字节上限";
      else { editor_ += input.event.text; editMessage_.clear(); }
    }
    return true;
  }
  if (screen_ == SettingsScreen::kViewer) {
    const bool retry = controller_.operation().phase == WifiPhase::kReleaseFailed && !controller_.operation().expired;
    if (key == Key::kBackspace || (key == Key::kEnter && !retry)) back();
    else if (key == Key::kEnter) controller_.retryClose();
    else {
      const size_t perPage = retry ? 4 : 5;
      const size_t count = std::max<size_t>(1, (wrap(viewText()).size() + perPage - 1) / perPage);
      if (key == Key::kTab) viewPage_ = (viewPage_ + 1) % count;
      if (key == Key::kRight || key == Key::kDown) viewPage_ = std::min(viewPage_ + 1, count - 1);
      if ((key == Key::kLeft || key == Key::kUp) && viewPage_) --viewPage_;
    }
    return true;
  }
  if (key == Key::kBackspace) { back(); return true; }
  if (screen_ == SettingsScreen::kCpuFrequency) {
    const auto selected = controller_.selectedCpuFrequencyMhz();
    auto next = selected;
    if ((key == Key::kLeft || key == Key::kUp) && next > 80) next -= 80;
    if ((key == Key::kRight || key == Key::kDown) && next < 240) next += 80;
    if (key == Key::kTab) next = next == 240 ? 80 : next + 80;
    if (next != selected) { controller_.setCpuFrequency(next); feedback(controller_.powerMessage()); }
    else if (key == Key::kEnter && controller_.powerSaveFailed()) {
      controller_.savePower(); feedback(controller_.powerMessage());
    }
    return true;
  }
  if (screen_ == SettingsScreen::kBrightness) {
    const int level = controller_.brightnessLevel();
    int next = level;
    if (key == Key::kLeft || key == Key::kUp) next = std::max(1, level - 1);
    if (key == Key::kRight || key == Key::kDown) next = std::min(5, level + 1);
    if (key == Key::kTab) next = level % 5 + 1;
    if (next != level) { controller_.setBrightness(next); feedback(controller_.displayMessage()); }
    else if (key == Key::kEnter && controller_.displaySaveFailed()) {
      controller_.saveDisplay(); feedback(controller_.displayMessage());
    }
    return true;
  }
  if (screen_ == SettingsScreen::kAutoScreenOff) {
    size_t index = 0;
    while (index < 4 && screenTimeouts[index] != controller_.autoScreenOffSeconds()) ++index;
    size_t next = index;
    if ((key == Key::kLeft || key == Key::kUp) && next) --next;
    if ((key == Key::kRight || key == Key::kDown) && next < 4) ++next;
    if (key == Key::kTab) next = (next + 1) % 5;
    if (next != index) { controller_.setAutoScreenOff(screenTimeouts[next]); feedback(controller_.displayMessage()); }
    else if (key == Key::kEnter && controller_.displaySaveFailed()) {
      controller_.saveDisplay(); feedback(controller_.displayMessage());
    }
    return true;
  }
  if (screen_ == SettingsScreen::kCodex) {
    const int seconds = minutes_.empty() ? controller_.intervalSeconds() : std::stoi(minutes_) * 60;
    int next = 0;
    if (key == Key::kLeft || key == Key::kUp) next = std::max(1, (seconds - 1) / 60);
    if (key == Key::kRight || key == Key::kDown) next = std::min(60, seconds / 60 + 1);
    if (key == Key::kTab) next = seconds / 60 % 60 + 1;
    if (next && next * 60 != seconds) {
      minutes_ = std::to_string(next); minutesDirty_ = true;
      saveScheduled_ = true; changedAt_ = now; feedback_.clear();
    }
    return true;
  }
  const auto count = rows().size();
  focus_ = std::min(focus_, count - 1);
  const auto previousFocus = focus_;
  if (key == Key::kTab) focus_ = (focus_ + 1) % count;
  if (key == Key::kDown && focus_ + 1 < count) ++focus_;
  if (key == Key::kUp && focus_) --focus_;
  if (focus_ != previousFocus) feedback_.clear();
  if (key != Key::kEnter) return true;
  feedback_.clear();
  if (screen_ == SettingsScreen::kHome) {
    homeFocus_ = focus_;
    constexpr SettingsScreen screens[] = {
        SettingsScreen::kBrightness, SettingsScreen::kWifi, SettingsScreen::kCodex,
        SettingsScreen::kAutoScreenOff, SettingsScreen::kCpuFrequency};
    enter(screens[focus_]);
  } else if (screen_ == SettingsScreen::kWifi) {
    if (focus_ < 3) edit(focus_);
    else if (focus_ == 3) { if (controller_.scan()) enter(SettingsScreen::kScan); else feedback(controller_.wifiMessage()); }
    else if (focus_ == 4) { controller_.saveWifi(); feedback(controller_.wifiMessage()); }
    else if (focus_ == 5) { controller_.test(); view(); }
    else view();
  } else if (screen_ == SettingsScreen::kScan) {
    if (focus_ < controller_.scans().count) {
      controller_.selectNetwork(focus_); screen_ = SettingsScreen::kWifi; focus_ = 0;
    } else if (focus_ == controller_.scans().count) { controller_.scan(); feedback(controller_.wifiMessage()); }
    else if (focus_ == controller_.scans().count + 1) back();
    else view();
  }
  return true;
}
std::vector<std::string> SettingsPage::rows() const {
  switch (screen_) {
    case SettingsScreen::kHome: return {"屏幕亮度 " + std::to_string(controller_.brightnessLevel()*20) + "%",
        "Wi-Fi " + controller_.wifiSummary(), "Codex自动刷新 " + (minutesDirty_ ? minutes_ + "分钟 未保存" : intervalText(controller_.intervalSeconds())),
        "自动息屏 " + screenTimeoutText(controller_.autoScreenOffSeconds()),
        "CPU频率 " + std::to_string(controller_.cpuFrequencyMhz()) + " MHz"};
    case SettingsScreen::kWifi: return {"SSID：" + controller_.draft().ssid, "用户名：" + controller_.draft().username,
        "密码：" + controller_.draft().password, "扫描网络", "保存", "测试连接", "查看网络信息"};
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
void SettingsPage::render() {
  const int width = display_.width();
  auto draw = [&](const std::string& text, int y, uint16_t color = color::kText) {
    display_.drawText(display_.fitText(text, width - 16), 8, y, color, FontStyle::kChinese);
  };
  if (screen_ == SettingsScreen::kBrightness) {
    draw("亮度", 25, color::kAccent);
    for (int i = 1; i <= 5; ++i) {
      const bool selected = controller_.brightnessLevel() == i;
      display_.fillRoundRect(8+(i-1)*45, 51, 42, 30, 4, selected ? color::kAccent : color::kSurface);
      display_.drawText(std::to_string(i*20)+"%", 11+(i-1)*45, 58, selected ? color::kAccentText : color::kText, FontStyle::kSmall);
    }
    if (!feedback_.empty()) draw(feedback_, 86, color::kMuted);
    return;
  }
  if (screen_ == SettingsScreen::kAutoScreenOff) {
    draw("自动息屏", 25, color::kAccent);
    draw("< " + screenTimeoutText(controller_.autoScreenOffSeconds()) + " >", 51);
    const auto text = controller_.displaySaveFailed() ? controller_.displayMessage() : feedback_;
    if (!text.empty()) draw(text, 78, controller_.displaySaveFailed() ? color::kRed : color::kMuted);
    if (controller_.displaySaveFailed()) draw("Enter重试保存", 100, color::kMuted);
    return;
  }
  if (screen_ == SettingsScreen::kCpuFrequency) {
    draw("CPU频率", 25, color::kAccent);
    draw("< " + std::to_string(controller_.cpuFrequencyMhz()) + " MHz >", 47);
    const auto message = controller_.powerSaveFailed() ? controller_.powerMessage() : feedback_;
    if (!message.empty()) draw(message, 70, controller_.powerSaveFailed() ? color::kRed : color::kMuted);
    if (controller_.powerSaveFailed())
      draw("Enter重试 " + std::to_string(controller_.selectedCpuFrequencyMhz()) + " MHz", 93, color::kMuted);
    return;
  }
  if (screen_ == SettingsScreen::kCodex) {
    draw("Codex自动刷新", 25, color::kAccent);
    draw("<  " + (minutes_.empty() ? intervalText(controller_.intervalSeconds()) : minutes_ + "分钟") + "  >", 51);
    if (minutesDirty_ && !saveScheduled_) draw(controller_.codexMessage(), 75, color::kRed);
    else if (!feedback_.empty()) draw(feedback_, 75, color::kMuted);
    return;
  }
  if (screen_ == SettingsScreen::kViewer) {
    const bool retry = controller_.operation().phase == WifiPhase::kReleaseFailed && !controller_.operation().expired;
    const size_t perPage = retry ? 4 : 5;
    const auto lines = wrap(viewText());
    const size_t count = std::max<size_t>(1, (lines.size()+perPage-1)/perPage);
    viewPage_ = std::min(viewPage_, count-1);
    draw("网络信息 " + std::to_string(viewPage_+1)+"/"+std::to_string(count), 25, color::kAccent);
    for (size_t i = viewPage_*perPage; i < lines.size() && i < (viewPage_+1)*perPage; ++i)
      draw(lines[i], 44+(i%perPage)*18);
    if (retry) {
      display_.fillRoundRect(4, 116, width-8, 18, 3, color::kSurface);
      draw("重试关闭Wi-Fi", 116, color::kAccent);
    }
    return;
  }
  const bool isHome = screen_ == SettingsScreen::kHome;
  if (!isHome) draw(screen_ == SettingsScreen::kWifi ? "Wi-Fi" : "扫描网络 " + controller_.wifiSummary(), 25, color::kAccent);
  const auto items = rows(); focus_ = std::min(focus_, items.size()-1);
  const size_t visible = isHome ? 4 : 5;
  const size_t start = focus_ < visible ? 0 : focus_ - visible + 1;
  for (size_t i = start; i < items.size() && i < start+visible; ++i) {
    const int y = (isHome ? 27 : 44)+(i-start)*18;
    if (i == focus_) display_.fillRoundRect(4, y, width-8, 18, 3, color::kSurface);
    if (screen_ == SettingsScreen::kWifi && i < 3) {
      const std::string label = std::string(fields[i])+"：";
      const int x = 8 + display_.textWidth(label, FontStyle::kChinese);
      draw(label, y, i == focus_ ? color::kAccent : color::kText);
      std::string value = editing_ && i == field_ ? editor_ + "_" : fieldValue(controller_.draft(), i);
      if (editing_ && i == field_) {
        while (!value.empty() && display_.textWidth(value, FontStyle::kChinese) > width-x-8) {
          size_t end = 1;
          while (end < value.size() && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80) ++end;
          value.erase(0, end);
        }
        display_.drawRect(x-2, y, width-x-4, 18, editMessage_.empty() ? color::kAccent : color::kRed);
        if (!editMessage_.empty()) value = editMessage_;
      } else {
        display_.drawRect(x-2, y, width-x-4, 18, color::kBorder);
        value = display_.fitText(value, width-x-8);
      }
      display_.drawText(value, x, y, color::kText, FontStyle::kChinese);
    } else draw(i == focus_ && !feedback_.empty() ? feedback_ : items[i], y, i == focus_ ? color::kAccent : color::kText);
  }
  if (isHome) draw("↑↓选择 enter进入 退格返回", display_.height()-18, color::kMuted);
}
}  // namespace adv
