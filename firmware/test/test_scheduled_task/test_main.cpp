#include <unity.h>
#include <vector>
#include "core/scheduled_task_service.h"
namespace adv {
struct ScheduledTaskTestAccess {
  static void fill(ScheduledTaskService& s) { for (auto& slot : s.slots_) slot.occupied = true; }
};
}
using namespace adv;
const auto A = ScheduledTaskId::kSystemTimeSync;
const auto B = ScheduledTaskId::kCodexUsageRefresh;
void lifecycle() {
  ScheduledTaskService s; int calls=0;
  TEST_ASSERT_FALSE(s.registerTask({A,999,true,false},[](uint32_t){},0));
  TEST_ASSERT_TRUE(s.registerTask({A,1000,true,false},[&](uint32_t){++calls;},100));
  TEST_ASSERT_FALSE(s.registerTask({A,1000,true,false},[](uint32_t){},0));
  s.setEnabled(A,true,900); s.tick(1100); TEST_ASSERT_EQUAL(1,calls);
  s.tick(10000); TEST_ASSERT_EQUAL(2,calls); s.tick(10000); TEST_ASSERT_EQUAL(2,calls);
  s.setEnabled(A,false,10000); s.tick(20000); TEST_ASSERT_EQUAL(2,calls);
  s.triggerNow(A,20000); TEST_ASSERT_EQUAL(3,calls);
  ScheduledTaskSnapshot state{}; s.getTask(A,state); TEST_ASSERT_FALSE(state.enabled);
  s.setEnabled(A,true,20000); s.tick(20999); TEST_ASSERT_EQUAL(3,calls);
  s.tick(21000); TEST_ASSERT_EQUAL(4,calls);
  s.updateInterval(A,2000,21000); s.rescheduleFromNow(A,22000); s.tick(23999); TEST_ASSERT_EQUAL(4,calls);
  s.tick(24000); TEST_ASSERT_EQUAL(5,calls);
  TEST_ASSERT_FALSE(s.updateInterval(A,0xffffffff,0));
  TEST_ASSERT_TRUE(s.cancel(A)); TEST_ASSERT_FALSE(s.cancel(A));
  TEST_ASSERT_FALSE(s.triggerNow(A,0));
}
void mutation_and_wrap() {
  ScheduledTaskService s; std::vector<int> calls;
  s.registerTask({A,1000,true,false},[&](uint32_t now){
    calls.push_back(1); TEST_ASSERT_FALSE(s.triggerNow(A,now)); s.tick(now);
    s.cancel(A); s.cancel(B);
    s.registerTask({B,1000,true,false},[&](uint32_t){calls.push_back(3);},now);
  },0xffffff00);
  s.registerTask({B,1000,true,false},[&](uint32_t){calls.push_back(2);},0xffffff00);
  s.tick(744); TEST_ASSERT_EQUAL(1,calls.size());
  s.tick(1744); TEST_ASSERT_EQUAL(2,calls.size()); TEST_ASSERT_EQUAL(3,calls.back());
}
void invalid_and_full() {
  ScheduledTaskService s;
  TEST_ASSERT_FALSE(s.registerTask({static_cast<ScheduledTaskId>(99),1000,true,false},[](uint32_t){},0));
  TEST_ASSERT_FALSE(s.registerTask({A,1000,true,false},{},0));
  ScheduledTaskTestAccess::fill(s);
  TEST_ASSERT_FALSE(s.registerTask({B,1000,true,false},[](uint32_t){},0));
}
void immediate_order_and_callback_updates() {
  ScheduledTaskService s; std::vector<int> calls;
  TEST_ASSERT_TRUE(s.registerTask({A,1000,true,true},[&](uint32_t){calls.push_back(1);},0));
  TEST_ASSERT_EQUAL(1,calls.size());
  s.registerTask({B,1000,false,true},[&](uint32_t){calls.push_back(2);},0);
  TEST_ASSERT_EQUAL(1,calls.size());
  s.setEnabled(B,true,0);
  s.cancel(A);
  s.registerTask({A,1000,true,false},[&](uint32_t now){
    calls.push_back(3);
    TEST_ASSERT_FALSE(s.registerTask({ScheduledTaskId::kDisplayRefresh,1000,true,true},[](uint32_t){},now));
    s.setEnabled(A,false,now);
    s.updateInterval(B,2000,now);
  },0);
  s.tick(1000);
  TEST_ASSERT_EQUAL(3,calls.size()); TEST_ASSERT_EQUAL(2,calls[1]); TEST_ASSERT_EQUAL(3,calls[2]);
  s.tick(2999); TEST_ASSERT_EQUAL(3,calls.size());
  s.tick(3000); TEST_ASSERT_EQUAL(4,calls.size());
  ScheduledTaskSnapshot unchanged{true,123};
  TEST_ASSERT_FALSE(s.getTask(ScheduledTaskId::kDisplayRefresh,unchanged));
  TEST_ASSERT_EQUAL(123,unchanged.intervalMs);
}
int main(int,char**) { UNITY_BEGIN(); RUN_TEST(lifecycle); RUN_TEST(mutation_and_wrap); RUN_TEST(invalid_and_full); RUN_TEST(immediate_order_and_callback_updates); return UNITY_END(); }
