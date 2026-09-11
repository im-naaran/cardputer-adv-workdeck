#include "platform/battery_adapter.h"
#ifdef ARDUINO
#include <M5Cardputer.h>
#endif
namespace adv {
BatterySnapshot PlatformBatteryAdapter::read() const {
#ifdef ARDUINO
  const int level = M5Cardputer.Power.getBatteryLevel();
  return {level,
          M5Cardputer.Power.isCharging() == m5::Power_Class::is_charging};
#else
  return {};
#endif
}
}  // namespace adv
