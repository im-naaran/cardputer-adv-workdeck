#include <unity.h>
#include "core/exec_id_generator.h"
void ids() {
  adv::ExecIdGenerator ids; std::string a,b;
  TEST_ASSERT_TRUE(ids.next(a)); TEST_ASSERT_TRUE(ids.next(b));
  TEST_ASSERT_EQUAL_STRING("0000000000000001",a.c_str());
  TEST_ASSERT_NOT_EQUAL(0,a.compare(b));
  adv::ExecIdGenerator end(UINT64_MAX-1);
  TEST_ASSERT_TRUE(end.next(a)); TEST_ASSERT_EQUAL_STRING("ffffffffffffffff",a.c_str());
  TEST_ASSERT_FALSE(end.next(b));
}
int main(int,char**) { UNITY_BEGIN(); RUN_TEST(ids); return UNITY_END(); }
