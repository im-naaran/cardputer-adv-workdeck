#include <unity.h>
#include "platform/keyboard_adapter.h"
using namespace adv;
void physical_activity() {
  KeyboardAdapter keyboard; keyboard.begin();
  for (auto member : {&InputSnapshot::fn, &InputSnapshot::alt, &InputSnapshot::ctrl,
                       &InputSnapshot::shift, &InputSnapshot::opt}) {
    InputSnapshot snapshot; snapshot.*member = true; keyboard.updateSnapshot(snapshot);
    TEST_ASSERT_TRUE(keyboard.activity().anyDown);
    TEST_ASSERT_TRUE(keyboard.activity().pressedThisUpdate);
    TEST_ASSERT_TRUE(keyboard.takePressedEvents().empty());
    keyboard.updateSnapshot(snapshot); TEST_ASSERT_FALSE(keyboard.activity().changed);
    keyboard.updateSnapshot({}); TEST_ASSERT_TRUE(keyboard.activity().changed);
    TEST_ASSERT_FALSE(keyboard.activity().anyDown);
  }
  InputSnapshot first; first.pressed['a']=true; keyboard.updateSnapshot(first);
  TEST_ASSERT_EQUAL(1,keyboard.takePressedEvents().size());
  InputSnapshot second; second.pressed['b']=true; keyboard.updateSnapshot(second);
  TEST_ASSERT_TRUE(keyboard.activity().changed); TEST_ASSERT_TRUE(keyboard.activity().pressedThisUpdate);
  TEST_ASSERT_EQUAL(1,keyboard.takePressedEvents().size());
  keyboard.updateSnapshot({}); TEST_ASSERT_TRUE(keyboard.takePressedEvents().empty());
}
int main(int,char**) { UNITY_BEGIN(); RUN_TEST(physical_activity); return UNITY_END(); }
