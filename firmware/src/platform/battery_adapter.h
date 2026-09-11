#pragma once
namespace adv {
struct BatterySnapshot {
  int level{-1};
  bool charging{false};
  bool operator==(const BatterySnapshot& other) const { return level == other.level && charging == other.charging; }
};
class PlatformBatteryAdapter {
 public:
  BatterySnapshot read() const;
};
}  // namespace adv
