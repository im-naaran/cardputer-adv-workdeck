#include "application/codex/codex_config.h"
#include <unity.h>

#include "application/codex/codex_page.h"
#include "core/protocol_constants.h"

namespace {
adv::Message response(const std::string& exec) {
  adv::Message message;
  message.actionId = adv::protocol::kCodexUsageAction;
  message.execId = exec;
  message.resultCode = "OK";
  message.fetchedAtEpochSeconds = 1000;
  message.windows = {{"a", "", "primary", -5, true, 300, true, 8200},
                     {"b", "审查额度", "secondary", 150, false, 0, false, 0},
                     {"c", "", "primary", 50, true, 10080, true, 1000}};
  return message;
}
adv::CodexUsageState loadedState() {
  adv::CodexUsageState state;
  state.onSessionReady();
  state.beginRequest("x");
  state.applyResponse(response("x"), 100);
  return state;
}
void test_converts_used_to_remaining_and_formats_missing_fields() {
  auto state = loadedState();
  adv::CodexPage page;
  auto view = page.makeView(state, 100, {true, adv::CodexConfig{}.refreshIntervalSeconds * 1000});
  TEST_ASSERT_EQUAL(100, view.rows[0].percent);
  TEST_ASSERT_EQUAL(0, view.rows[1].percent);
  TEST_ASSERT_EQUAL_STRING("5小时额度", view.rows[0].title.c_str());
  TEST_ASSERT_EQUAL_STRING("重置 --", view.rows[1].resetText.c_str());
}
void test_unsupported_dynamic_name_falls_back() {
  auto state = loadedState();
  auto message = response("y");
  message.windows[0].limitName = "Codex 🚀";
  state.beginRequest("y");
  state.applyResponse(message, 101);
  adv::CodexPage page;
  TEST_ASSERT_EQUAL_STRING("5小时额度", page.makeView(state, 101, {true, adv::CodexConfig{}.refreshIntervalSeconds * 1000}).rows[0].title.c_str());
}
void test_long_dynamic_name_is_utf8_safely_truncated() {
  auto state = loadedState();
  auto message = response("long");
  message.windows[0].limitName = "这是一个非常非常长的额度窗口名称";
  state.beginRequest("long");
  state.applyResponse(message, 102);
  adv::CodexPage page;
  const std::string title = page.makeView(state, 102, {true, adv::CodexConfig{}.refreshIntervalSeconds * 1000}).rows[0].title;
  TEST_ASSERT_TRUE(title.size() < message.windows[0].limitName.size());
  TEST_ASSERT_EQUAL_STRING("..", title.substr(title.size() - 2).c_str());
}
void test_relative_time_and_reset_boundaries() {
  auto state = loadedState();
  adv::CodexPage page;
  auto view = page.makeView(state, 180100, {true, adv::CodexConfig{}.refreshIntervalSeconds * 1000});
  TEST_ASSERT_EQUAL_STRING("3分钟前", view.footer.c_str());
  TEST_ASSERT_EQUAL_STRING("即将重置", view.rows[2].resetText.c_str());
}
void test_scroll_is_bounded() {
  auto state = loadedState();
  adv::CodexPage page;
  page.scroll(1, state.windows().size());
  page.scroll(1, state.windows().size());
  TEST_ASSERT_EQUAL_UINT32(1, page.scrollOffset());
  page.scroll(-1, state.windows().size());
  page.scroll(-1, state.windows().size());
  TEST_ASSERT_EQUAL_UINT32(0, page.scrollOffset());
}
void test_loading_and_error_copy() {
  adv::CodexUsageState state;
  state.onSessionReady();
  state.beginRequest("x");
  adv::CodexPage page;
  auto loading = page.makeView(state, 0, {true, adv::CodexConfig{}.refreshIntervalSeconds * 1000});
  TEST_ASSERT_EQUAL_STRING("正在查询用量", loading.centerMessage.c_str());
  TEST_ASSERT_EQUAL_STRING("请稍候", loading.centerHint.c_str());
  adv::Message error;
  error.actionId = adv::protocol::kCodexUsageAction;
  error.execId = "x";
  error.resultCode = "NOT_LOGGED_IN";
  state.applyResponse(error, 1);
  auto notLoggedIn = page.makeView(state, 1, {true, adv::CodexConfig{}.refreshIntervalSeconds * 1000});
  TEST_ASSERT_EQUAL_STRING("请先登录 Codex", notLoggedIn.centerMessage.c_str());
  TEST_ASSERT_EQUAL_STRING("登录后按 Enter 重试", notLoggedIn.centerHint.c_str());
}
void test_automatic_label_in_all_states() {
  adv::CodexPage page;
  adv::CodexUsageState state;
  state.onSessionReady();
  TEST_ASSERT_EQUAL_STRING("自动 5m",page.makeView(state,0,{true,300000}).automaticLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("自动 1h",page.makeView(state,0,{true,3600000}).automaticLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("自动 61s",page.makeView(state,0,{true,61000}).automaticLabel.c_str());
  state.beginRequest("pending");
  auto loading=page.makeView(state,0,{false,300000});
  TEST_ASSERT_TRUE(loading.refreshing);
  TEST_ASSERT_EQUAL_STRING("自动已关",loading.automaticLabel.c_str());
  state.failRequest("TIMEOUT");
  auto error=page.makeView(state,1,{false,300000});
  TEST_ASSERT_EQUAL_STRING("查询超时",error.centerMessage.c_str());
  TEST_ASSERT_EQUAL_STRING("自动已关",error.automaticLabel.c_str());
  auto loaded=loadedState();
  TEST_ASSERT_EQUAL_STRING("自动已关",page.makeView(loaded,100,{false,300000}).automaticLabel.c_str());
}
}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_converts_used_to_remaining_and_formats_missing_fields);
  RUN_TEST(test_unsupported_dynamic_name_falls_back);
  RUN_TEST(test_long_dynamic_name_is_utf8_safely_truncated);
  RUN_TEST(test_relative_time_and_reset_boundaries);
  RUN_TEST(test_scroll_is_bounded);
  RUN_TEST(test_loading_and_error_copy);
  RUN_TEST(test_automatic_label_in_all_states);
  return UNITY_END();
}
