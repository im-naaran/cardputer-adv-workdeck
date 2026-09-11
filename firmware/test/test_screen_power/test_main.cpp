#include <unity.h>
#include "application/power/screen_power_controller.h"
using namespace adv;
void timeout_and_wake() {
  for (uint32_t seconds : {60u, 300u, 600u, 1800u}) {
    ScreenPowerController power; power.begin(0); power.setTimeout(seconds);
    power.tick(seconds*1000-1); TEST_ASSERT_TRUE(power.visible());
    power.tick(seconds*1000); TEST_ASSERT_FALSE(power.visible());
    TEST_ASSERT_FALSE(power.onKeyboard({true,true,true}, seconds*1000+1));
    TEST_ASSERT_TRUE(power.visible());
    TEST_ASSERT_FALSE(power.onKeyboard({true,false,false}, seconds*1000+10000));
    power.tick(seconds*1000+999999); TEST_ASSERT_TRUE(power.visible());
    TEST_ASSERT_FALSE(power.onKeyboard({false,true,false}, seconds*1000+1000000));
    TEST_ASSERT_TRUE(power.onKeyboard({true,true,true}, seconds*1000+1000001));
  }
}
void activity_and_wrap() {
  ScreenPowerController power; power.begin(UINT32_MAX-30000); power.setTimeout(60);
  power.tick(29998); TEST_ASSERT_TRUE(power.visible());
  power.tick(29999); TEST_ASSERT_FALSE(power.visible());
  power.begin(0); power.setTimeout(0); power.tick(2000000); TEST_ASSERT_TRUE(power.visible());
  power.setTimeout(60);
  TEST_ASSERT_TRUE(power.onKeyboard({true,true,true}, 2000000));
  power.tick(3000000); TEST_ASSERT_TRUE(power.visible());
  power.onKeyboard({false,true,false},3000000);
  power.tick(3059999); TEST_ASSERT_TRUE(power.visible());
  power.tick(3060000); TEST_ASSERT_FALSE(power.visible());
}
int main(int,char**) { UNITY_BEGIN(); RUN_TEST(timeout_and_wake); RUN_TEST(activity_and_wrap); return UNITY_END(); }
