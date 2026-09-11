#pragma once
#include <functional>
#include <utility>
#include "platform/battery_adapter.h"
namespace adv {
class BatteryService {
 public:
  explicit BatteryService(std::function<BatterySnapshot()> read) : read_(std::move(read)) {}
  bool sample();
  const BatterySnapshot& snapshot() const { return snapshot_; }
 private:
  std::function<BatterySnapshot()> read_;
  BatterySnapshot snapshot_{};
};
}  // namespace adv
