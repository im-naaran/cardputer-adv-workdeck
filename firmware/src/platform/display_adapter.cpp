#include "platform/display_adapter.h"

#ifdef ARDUINO
#include <M5Cardputer.h>
#include <M5GFX.h>
#endif

namespace adv {

#ifdef ARDUINO
namespace {
// Cardputer-ADV has no PSRAM in the current board profile. LGFX_Sprite uses
// internal DMA memory, while M5Canvas prefers PSRAM and would silently fall
// back to direct drawing on this device.
LGFX_Sprite frame(&M5Cardputer.Display);
bool frameReady = false;
bool frameAttempted = false;

template <typename Gfx>
void applyFont(Gfx& gfx, FontStyle style) {
  if (style == FontStyle::kChinese) gfx.setFont(&fonts::efontCN_16);
  else if (style == FontStyle::kSmall) gfx.setFont(&fonts::Font0);
  else gfx.setFont(&fonts::Font2);
  gfx.setTextSize(1);
  gfx.setTextDatum(top_left);
}
}  // namespace
#endif

void DisplayAdapter::begin() {
#ifdef ARDUINO
  auto config = M5.config();
  M5Cardputer.begin(config, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextDatum(top_left);
  frame.setColorDepth(16);
#endif
}

void DisplayAdapter::setBrightness(uint8_t brightness) {
#ifdef ARDUINO
  M5Cardputer.Display.setBrightness(brightness);
#else
  (void)brightness;
#endif
}

int DisplayAdapter::width() const {
#ifdef ARDUINO
  return M5Cardputer.Display.width();
#else
  return 240;
#endif
}

int DisplayAdapter::height() const {
#ifdef ARDUINO
  return M5Cardputer.Display.height();
#else
  return 135;
#endif
}

int DisplayAdapter::batteryLevel() const {
#ifdef ARDUINO
  const int level = M5Cardputer.Power.getBatteryLevel();
  return level >= 0 && level <= 100 ? level : -1;
#else
  return -1;
#endif
}

bool DisplayAdapter::batteryCharging() const {
#ifdef ARDUINO
  return M5Cardputer.Power.isCharging() == m5::Power_Class::is_charging;
#else
  return false;
#endif
}

void DisplayAdapter::beginFrame(uint16_t color) {
#ifdef ARDUINO
  // Allocation is delayed until the first frame, after BLE initialization in
  // setup(), so the optional framebuffer cannot starve the transport stack.
  if (!frameAttempted) {
    frameAttempted = true;
    frameReady = frame.createSprite(M5Cardputer.Display.width(),
                                    M5Cardputer.Display.height()) != nullptr;
    if (frameReady) frame.setTextDatum(top_left);
  }
  if (frameReady) frame.fillSprite(color);
  else {
    M5Cardputer.Display.startWrite();
    M5Cardputer.Display.fillScreen(color);
  }
#else
  (void)color;
#endif
}

void DisplayAdapter::endFrame() {
#ifdef ARDUINO
  if (frameReady) frame.pushSprite(0, 0);
  else M5Cardputer.Display.endWrite();
#endif
}

void DisplayAdapter::fillRect(int x, int y, int w, int h, uint16_t color) {
#ifdef ARDUINO
  if (frameReady) frame.fillRect(x, y, w, h, color);
  else M5Cardputer.Display.fillRect(x, y, w, h, color);
#else
  (void)x; (void)y; (void)w; (void)h; (void)color;
#endif
}

void DisplayAdapter::fillRoundRect(int x, int y, int w, int h, int radius,
                                   uint16_t color) {
#ifdef ARDUINO
  if (frameReady) frame.fillRoundRect(x, y, w, h, radius, color);
  else M5Cardputer.Display.fillRoundRect(x, y, w, h, radius, color);
#else
  (void)x; (void)y; (void)w; (void)h; (void)radius; (void)color;
#endif
}

void DisplayAdapter::drawRect(int x, int y, int w, int h, uint16_t color) {
#ifdef ARDUINO
  if (frameReady) frame.drawRect(x, y, w, h, color);
  else M5Cardputer.Display.drawRect(x, y, w, h, color);
#else
  (void)x; (void)y; (void)w; (void)h; (void)color;
#endif
}

void DisplayAdapter::drawRoundRect(int x, int y, int w, int h, int radius,
                                   uint16_t color) {
#ifdef ARDUINO
  if (frameReady) frame.drawRoundRect(x, y, w, h, radius, color);
  else M5Cardputer.Display.drawRoundRect(x, y, w, h, radius, color);
#else
  (void)x; (void)y; (void)w; (void)h; (void)radius; (void)color;
#endif
}

void DisplayAdapter::drawLine(int x0, int y0, int x1, int y1, uint16_t color) {
#ifdef ARDUINO
  if (frameReady) frame.drawLine(x0, y0, x1, y1, color);
  else M5Cardputer.Display.drawLine(x0, y0, x1, y1, color);
#else
  (void)x0; (void)y0; (void)x1; (void)y1; (void)color;
#endif
}

void DisplayAdapter::fillCircle(int x, int y, int radius, uint16_t color) {
#ifdef ARDUINO
  if (frameReady) frame.fillCircle(x, y, radius, color);
  else M5Cardputer.Display.fillCircle(x, y, radius, color);
#else
  (void)x; (void)y; (void)radius; (void)color;
#endif
}

void DisplayAdapter::drawText(const std::string& text, int x, int y,
                              uint16_t color, FontStyle style) {
#ifdef ARDUINO
  if (frameReady) {
    applyFont(frame, style);
    frame.setTextColor(color);
    frame.drawString(text.c_str(), x, y);
  } else {
    applyFont(M5Cardputer.Display, style);
    M5Cardputer.Display.setTextColor(color);
    M5Cardputer.Display.drawString(text.c_str(), x, y);
  }
#else
  (void)text; (void)x; (void)y; (void)color; (void)style;
#endif
}

std::string DisplayAdapter::fitText(const std::string& text, int maxWidth, FontStyle style) {
  if (text.empty() || maxWidth <= 0) return {};
  if (textWidth(text, style) <= maxWidth) return text;
  if (textWidth("..", style) > maxWidth) return {};
  std::string fitted = text;
  // Measure in the active font and remove whole UTF-8 characters, never bytes
  // from a Chinese name. The original action name/ID remains intact.
  do {
    size_t last = fitted.size() - 1;
    while (last > 0 && (static_cast<unsigned char>(fitted[last]) & 0xc0) == 0x80) --last;
    fitted.resize(last);
  } while (!fitted.empty() && textWidth(fitted + "..", style) > maxWidth);
  return fitted + "..";
}

int DisplayAdapter::textWidth(const std::string& text, FontStyle style) {
#ifdef ARDUINO
  if (frameReady) {
    applyFont(frame, style);
    return frame.textWidth(text.c_str());
  }
  applyFont(M5Cardputer.Display, style);
  return M5Cardputer.Display.textWidth(text.c_str());
#else
  int units = 0;
  for (unsigned char byte : text) {
    if ((byte & 0xC0) != 0x80) units += byte < 0x80 ? 1 : 2;
  }
  return units * (style == FontStyle::kSmall ? 6 : 8);
#endif
}

}  // namespace adv
