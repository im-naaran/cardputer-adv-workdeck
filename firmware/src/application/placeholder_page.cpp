#include "application/placeholder_page.h"

namespace adv {

void PlaceholderPage::render(DisplayAdapter& display, Module module) const {
  const char* title = "设置";
  if (module == Module::kClipboard) title = "剪贴板";
  display.fillRoundRect(12, 32, 216, 82, 8, color::kSurface);
  display.drawRoundRect(12, 32, 216, 82, 8, color::kBorder);
  display.fillRoundRect(105, 39, 30, 20, 6, color::kAccent);
  display.drawLine(113, 49, 127, 49, color::kAccentText);
  display.drawLine(120, 42, 120, 56, color::kAccentText);
  const std::string titleText = title;
  const std::string hint = "功能将在后续版本开放";
  display.drawText(titleText,
                   (display.width() - display.textWidth(titleText, FontStyle::kChinese)) / 2,
                   65, color::kText, FontStyle::kChinese);
  display.drawText(hint,
                   (display.width() - display.textWidth(hint, FontStyle::kChinese)) / 2,
                   88, color::kMuted, FontStyle::kChinese);
}

}  // namespace adv
