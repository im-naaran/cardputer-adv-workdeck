#include <unity.h>
#include "application/power/battery_service.h"
#include "core/scheduled_task_service.h"
using namespace adv;
void cadence_and_cache() {
  int reads = 0; BatterySnapshot value{70,false};
  BatteryService battery([&]{ ++reads; return value; });
  ScheduledTaskService scheduler;
  const uint32_t start = UINT32_MAX-30000;
  TEST_ASSERT_TRUE(scheduler.registerTask({ScheduledTaskId::kBatterySample,60000,true,true},
      [&](uint32_t){ battery.sample(); }, start));
  TEST_ASSERT_EQUAL(1,reads);
  for (int i=0;i<100;++i) TEST_ASSERT_EQUAL(70,battery.snapshot().level);
  scheduler.tick(start+59999); TEST_ASSERT_EQUAL(1,reads);
  value={101,false}; scheduler.tick(start+60000); TEST_ASSERT_EQUAL(2,reads);
  TEST_ASSERT_EQUAL(-1,battery.snapshot().level);
  scheduler.tick(start+60001); TEST_ASSERT_EQUAL(2,reads);
  scheduler.tick(start+600000); TEST_ASSERT_EQUAL(3,reads);
  scheduler.tick(start+600000); TEST_ASSERT_EQUAL(3,reads);
  value={30,true}; TEST_ASSERT_TRUE(battery.sample());
  TEST_ASSERT_FALSE(battery.sample()); TEST_ASSERT_TRUE(battery.snapshot().charging);
}
int main(int,char**) { UNITY_BEGIN(); RUN_TEST(cadence_and_cache); return UNITY_END(); }
