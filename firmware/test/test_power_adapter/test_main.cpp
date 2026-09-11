#include <unity.h>
#include "fixture.h"
using namespace adv;
void fixed_frequency_and_capability() {
  FakePower power;
  for (auto mhz : {160u,80u,240u}) {
    const auto result = power.configure(mhz);
    TEST_ASSERT_TRUE(result.status == PowerStatus::kOk);
    TEST_ASSERT_EQUAL(mhz, result.frequencyMhz);
  }
  const auto calls = power.calls.size();
  TEST_ASSERT_TRUE(power.configure(240).status == PowerStatus::kOk);
  for (auto mhz : {0u,40u,120u,320u,0xffffffffu})
    TEST_ASSERT_TRUE(power.configure(mhz).status == PowerStatus::kInvalidFrequency);
  TEST_ASSERT_TRUE(power.configure(80,true).status == PowerStatus::kUnsupported);
  TEST_ASSERT_EQUAL(calls,power.calls.size());
  TEST_ASSERT_EQUAL(240,power.frequencyMhz());
  power.actual=0;
  TEST_ASSERT_TRUE(power.configure(160).status == PowerStatus::kUnsupported);
}
void failures_restore_and_report_actual() {
  FakePower power;
  power.fail=true;
  auto result=power.configure(160);
  TEST_ASSERT_TRUE(result.status==PowerStatus::kApplyFailed);
  TEST_ASSERT_EQUAL(240,result.frequencyMhz);
  power.fail=false;
  // A reported success with wrong hardware readback also rolls back.
  power.steps={{true,80},{true,240}};
  result=power.configure(160);
  TEST_ASSERT_TRUE(result.status==PowerStatus::kApplyFailed);
  TEST_ASSERT_EQUAL(240,result.frequencyMhz);
  TEST_ASSERT_EQUAL(240,power.calls.back());
  power.steps={{false,160},{true,240}};
  result=power.configure(160);
  TEST_ASSERT_TRUE(result.status==PowerStatus::kApplyFailed);
  TEST_ASSERT_EQUAL(240,result.frequencyMhz);
  power.steps={{true,80},{false,80}};
  result=power.configure(160);
  TEST_ASSERT_TRUE(result.status==PowerStatus::kRestoreFailed);
  TEST_ASSERT_EQUAL(80,result.frequencyMhz);
  TEST_ASSERT_TRUE(power.configure(240).status==PowerStatus::kOk);
  power.steps={{false,160},{true,80}};
  result=power.configure(160);
  TEST_ASSERT_TRUE(result.status==PowerStatus::kRestoreFailed);
  TEST_ASSERT_EQUAL(80,result.frequencyMhz);
}
int main(int,char**) { UNITY_BEGIN(); RUN_TEST(fixed_frequency_and_capability); RUN_TEST(failures_restore_and_report_actual); return UNITY_END(); }
