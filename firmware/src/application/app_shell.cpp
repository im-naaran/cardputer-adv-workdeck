#include "application/app_shell.h"

namespace adv {

void AppShell::beginFrame(DisplayAdapter& display, Module current,
                          const BatterySnapshot& battery) const {
  display.beginFrame(color::kBackground);
  display.fillRoundRect(4, 3, display.width() - 8, 18, 9, color::kAccent);
  for (int index = 0; index < 4; ++index) {
    drawModuleIcon(display, index, 6 + index * 31,
                   index == static_cast<int>(current));
  }
  // The fifth 31 px slot remains available for a future module.
  drawBatteryLevel(display, battery.level, battery.charging);
}

void AppShell::drawModuleIcon(DisplayAdapter& display, int index, int x, bool selected) const {
  const uint16_t foreground = selected ? color::kAccent : color::kAccentText;
  if (selected) display.fillRoundRect(x, 5, 27, 14, 5, color::kSurface);
  if (index == 0) {
    display.drawRoundRect(x + 6, 8, 14, 7, 1, foreground);
    display.drawLine(x + 8, 16, x + 18, 16, foreground);
  } else if (index == 1) {
    display.drawLine(x + 6, 8, x + 10, 11, foreground);
    display.drawLine(x + 10, 11, x + 6, 14, foreground);
    display.drawLine(x + 13, 15, x + 20, 15, foreground);
  } else if (index == 2) {
    display.drawRoundRect(x + 8, 7, 11, 10, 1, foreground);
    display.drawLine(x + 6, 9, x + 6, 15, foreground);
    display.drawLine(x + 6, 15, x + 16, 15, foreground);
  } else {
    display.drawRoundRect(x + 7, 7, 13, 10, 2, foreground);
    display.fillCircle(x + 13, 12, 2, foreground);
  }
}

void AppShell::drawBatteryLevel(DisplayAdapter& display, int batteryLevel,
                                bool charging) const {
  // Keep the cap clear of the rounded system bar's right edge.
  const int bodyX = 207;
  display.drawRoundRect(bodyX, 7, 24, 10, 2, color::kAccentText);
  display.fillRect(bodyX + 24, 10, 2, 4, color::kAccentText);
  if (batteryLevel >= 0 && batteryLevel <= 100) {
    const uint16_t tint = batteryLevel < 20 ? color::kRed : color::kAccentText;
    const int fillWidth = 20 * batteryLevel / 100;
    if (fillWidth > 0) display.fillRoundRect(bodyX + 2, 9, fillWidth, 6, 1, tint);
    const std::string label = std::to_string(batteryLevel) + "%";
    display.drawText(label,
                     bodyX - 6 - display.textWidth(label, FontStyle::kSmall), 8,
                     color::kAccentText, FontStyle::kSmall);
  } else {
    const std::string label = "--";
    display.drawText(label,
                     bodyX - 6 - display.textWidth(label, FontStyle::kSmall), 8,
                     color::kBorder, FontStyle::kSmall);
  }
  if (charging) {
    display.drawLine(bodyX + 11, 8, bodyX + 8, 12, color::kAmber);
    display.drawLine(bodyX + 8, 12, bodyX + 13, 12, color::kAmber);
    display.drawLine(bodyX + 13, 12, bodyX + 10, 16, color::kAmber);
  }
}

void AppShell::renderDisconnected(DisplayAdapter& display) const {
  display.fillRoundRect(12, 32, 216, 82, 8, color::kSurface);
  display.drawRoundRect(12, 32, 216, 82, 8, color::kBorder);
  display.fillCircle(120, 47, 8, color::kAccent);
  display.drawLine(116, 47, 124, 47, color::kAccentText);
  display.drawLine(120, 43, 120, 51, color::kAccentText);
  const std::string title = "等待电脑连接";
  const std::string hint = "请启动桌面端服务";
  display.drawText(title, (display.width() - display.textWidth(title, FontStyle::kChinese)) / 2,
                   62, color::kText, FontStyle::kChinese);
  display.drawText(hint, (display.width() - display.textWidth(hint, FontStyle::kChinese)) / 2,
                   87, color::kMuted, FontStyle::kChinese);
}

void AppShell::renderFeedback(DisplayAdapter& display, const std::string& text) const {
  if (text.empty()) return;
  // A temporary content footer overlays page detail without touching the system bar.
  const int y = display.height() - 20;
  display.fillRect(0, y, display.width(), 20, color::kSurface);
  display.drawLine(5, y, display.width()-5, y, color::kAccent);
  display.drawText(display.fitText(text, display.width()-12), 6, y+2, color::kText, FontStyle::kChinese);
}

void AppShell::endFrame(DisplayAdapter& display) const { display.endFrame(); }

}  // namespace adv
