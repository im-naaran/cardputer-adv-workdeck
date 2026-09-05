#pragma once

#include <cstdint>
#include <string>

namespace adv {

enum class FontStyle {
  kSmall,
  kBody,
  kChinese,
};

class DisplayAdapter {
 public:
  void begin();
  int width() const;
  int height() const;
  int batteryLevel() const;
  bool batteryCharging() const;
  void beginFrame(uint16_t color);
  void endFrame();
  void fillRect(int x, int y, int w, int h, uint16_t color);
  void fillRoundRect(int x, int y, int w, int h, int radius, uint16_t color);
  void drawRect(int x, int y, int w, int h, uint16_t color);
  void drawRoundRect(int x, int y, int w, int h, int radius, uint16_t color);
  void drawLine(int x0, int y0, int x1, int y1, uint16_t color);
  void fillCircle(int x, int y, int radius, uint16_t color);
  void drawText(const std::string& text, int x, int y, uint16_t color,
                FontStyle style = FontStyle::kBody);
  int textWidth(const std::string& text, FontStyle style = FontStyle::kBody);
};

namespace color {
// M5Cardputer UserDemo-inspired charcoal shell and lime system accent.
// Values are RGB565 equivalents of the reference theme's RGB888 colors.
constexpr uint16_t kBackground = 0x3186;  // #333333
constexpr uint16_t kSurface = 0x4228;     // #444444
constexpr uint16_t kText = 0xE73C;        // #E6E6E6
constexpr uint16_t kMuted = 0xAD55;       // #AAAAAA
constexpr uint16_t kBorder = 0x630C;      // #666666
constexpr uint16_t kAccent = 0x9FE0;      // #99FF00
constexpr uint16_t kAccentText = 0x0000;
constexpr uint16_t kAmber = 0xFD20;
constexpr uint16_t kRed = 0xF800;
constexpr uint16_t kTrack = 0x630C;
constexpr uint16_t kWhite = 0xFFFF;
}  // namespace color

}  // namespace adv
