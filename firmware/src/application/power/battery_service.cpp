#include "application/power/battery_service.h"
namespace adv {
bool BatteryService::sample() {
  auto next = read_();
  if (next.level < 0 || next.level > 100) next.level = -1;
  const bool changed = !(next == snapshot_);
  snapshot_ = next;
  return changed;
}
}  // namespace adv
