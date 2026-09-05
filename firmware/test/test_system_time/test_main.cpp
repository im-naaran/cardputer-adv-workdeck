#include <unity.h>
#include "core/system_time_service.h"
class FakeClock : public adv::SystemClock {
 public:
  int64_t value{0}; bool fail{false};
  bool setUtcMilliseconds(int64_t v) override { if(fail) return false; value=v; return true; }
  bool readUtcMilliseconds(int64_t& v) const override { v=value; return true; }
  int64_t minSeconds() const override { return INT32_MIN; }
  int64_t maxSeconds() const override { return INT32_MAX; }
};
void time_state() {
  FakeClock clock; adv::SystemTimeService service(clock); int64_t now=99;
  TEST_ASSERT_FALSE(service.currentUtcMilliseconds(now)); TEST_ASSERT_EQUAL(99,now);
  TEST_ASSERT_TRUE(service.synchronize(1788480000123LL,480,50));
  TEST_ASSERT_TRUE(service.currentUtcMilliseconds(now)); TEST_ASSERT_EQUAL_INT64(1788480000123LL,now);
  tm local{}; TEST_ASSERT_TRUE(service.currentLocalTime(local));
  const time_t expectedSeconds = static_cast<time_t>(1788480000LL + 480*60);
  tm expected{}; gmtime_r(&expectedSeconds,&expected);
  TEST_ASSERT_EQUAL(expected.tm_hour,local.tm_hour);
  TEST_ASSERT_EQUAL(expected.tm_mday,local.tm_mday);
  TEST_ASSERT_TRUE(service.synchronize(1000,-60,60)); // Backward correction is legal.
  TEST_ASSERT_EQUAL(60,service.lastSuccessfulSyncMs());
  clock.fail=true; TEST_ASSERT_FALSE(service.synchronize(999999,0,70));
  TEST_ASSERT_EQUAL(-60,service.utcOffsetMinutes()); TEST_ASSERT_EQUAL(60,service.lastSuccessfulSyncMs());
  TEST_ASSERT_EQUAL_INT64(1000,clock.value);
}
void bounds() {
  FakeClock clock; adv::SystemTimeService service(clock);
  TEST_ASSERT_FALSE(service.synchronize(-1,0,0));
  TEST_ASSERT_FALSE(service.synchronize(0,841,0));
  TEST_ASSERT_FALSE(service.synchronize(0,-721,0));
  TEST_ASSERT_FALSE(service.synchronize(INT64_MAX,0,0));
  TEST_ASSERT_FALSE(service.synchronize((static_cast<int64_t>(INT32_MAX)+1)*1000,0,0));
  TEST_ASSERT_FALSE(service.synchronize(static_cast<int64_t>(INT32_MAX)*1000,1,0));
  TEST_ASSERT_TRUE(service.synchronize(static_cast<int64_t>(INT32_MAX)*1000+999,0,0));
  TEST_ASSERT_TRUE(service.synchronize(0,-720,0));
  adv::PlatformSystemClock platform;
  TEST_ASSERT_FALSE(platform.setUtcMilliseconds(1000)); // Never change host clock in native tests.
}
int main(int,char**) { UNITY_BEGIN(); RUN_TEST(time_state); RUN_TEST(bounds); return UNITY_END(); }
