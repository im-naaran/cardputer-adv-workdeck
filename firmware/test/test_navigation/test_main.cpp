#include <unity.h>

#include "core/navigation_service.h"

namespace {

void test_defaults_to_codex_and_direct_navigation() {
  adv::NavigationService nav;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(adv::Module::kCodex), static_cast<int>(nav.current()));
  TEST_ASSERT_TRUE(nav.handleGlobal({adv::Key::kDigit4, true}));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(adv::Module::kSettings), static_cast<int>(nav.current()));
}

void test_navigation_requires_fn() {
  adv::NavigationService nav;
  TEST_ASSERT_FALSE(nav.handleGlobal({adv::Key::kDigit2, false}));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(adv::Module::kCodex), static_cast<int>(nav.current()));
}

void test_left_and_right_wrap() {
  adv::NavigationService nav;
  nav.handleGlobal({adv::Key::kLeft, true});
  TEST_ASSERT_EQUAL_INT(static_cast<int>(adv::Module::kSettings), static_cast<int>(nav.current()));
  nav.handleGlobal({adv::Key::kRight, true});
  TEST_ASSERT_EQUAL_INT(static_cast<int>(adv::Module::kCodex), static_cast<int>(nav.current()));
}

void test_non_global_key_is_not_consumed() {
  adv::NavigationService nav;
  TEST_ASSERT_FALSE(nav.handleGlobal({adv::Key::kEnter, true}));
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_to_codex_and_direct_navigation);
  RUN_TEST(test_navigation_requires_fn);
  RUN_TEST(test_left_and_right_wrap);
  RUN_TEST(test_non_global_key_is_not_consumed);
  return UNITY_END();
}
