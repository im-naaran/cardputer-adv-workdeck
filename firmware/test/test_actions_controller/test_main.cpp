#include <unity.h>
#include <ArduinoJson.h>
#include <fstream>
#include <sstream>
#include "application/actions/actions_controller.h"
#include "core/protocol_constants.h"
using namespace adv;
constexpr auto S = ActionType::kScript;
constexpr auto C = ActionType::kClipboard;
struct Harness {
  ExecIdGenerator ids;
  std::vector<std::string> sent, cancelled;
  ActionsController c{ids, [&](const auto&, const auto& bytes, uint32_t) { sent.push_back(bytes); return true; },
                          [&](const auto& id) { cancelled.push_back(id); }};
  void ready() { c.onSessionReady("pc", true, true, true, {S,C}); }
  Message fixture(const std::string& name) {
    std::ifstream file("../protocol/fixtures/v2/" + name + ".json"); std::stringstream buffer; buffer << file.rdbuf();
    auto result = MessageCodec().decode(buffer.str()); TEST_ASSERT_TRUE_MESSAGE(result.ok, name.c_str()); return result.message;
  }
  Message page(ActionType type, bool last = false) {
    auto m = fixture(std::string(actionTypeName(type)) + (last ? "_page_last" : "_page_first"));
    m.execId = c.directoryExecId(type); return m;
  }
  Message result(const std::string& name = "clipboard_execute_success") {
    auto m = fixture(name); m.execId = c.executionExecId(); return m;
  }
};
void lazy_directories_reverse_order_and_independent_selection() {
  Harness h; h.ready(); TEST_ASSERT_TRUE(h.sent.empty());
  h.c.enter(S,0); h.c.enter(C,0); h.c.enter(S,0); TEST_ASSERT_EQUAL(2,h.sent.size());
  const auto s = h.page(S), c = h.page(C);
  TEST_ASSERT_TRUE(h.c.onMessage(c,1)); TEST_ASSERT_TRUE(h.c.onMessage(s,2));
  h.c.moveSelection(C,1,3); h.c.moveSelection(C,1,3); h.c.moveSelection(S,1,3);
  h.c.enter(C,4); TEST_ASSERT_EQUAL(2,h.c.state(C).selected); TEST_ASSERT_EQUAL(1,h.c.state(S).selected);
  for (int i=0;i<6;++i) h.c.moveSelection(C,1,5);
  TEST_ASSERT_FALSE(h.c.confirm(C,6)); TEST_ASSERT_TRUE(h.c.onMessage(h.page(C,true),7));
  TEST_ASSERT_EQUAL(8,h.c.state(C).offset); TEST_ASSERT_EQUAL(1,h.c.state(S).selected);
  h.c.moveSelection(C,-1,8); TEST_ASSERT_TRUE(h.c.onMessage(h.page(C),9)); TEST_ASSERT_EQUAL(7,h.c.state(C).selected);
}
void wrong_type_retry_and_late_directory() {
  Harness h; h.ready(); h.c.enter(C,0); h.c.enter(S,0);
  auto wrong = h.page(S); wrong.execId = h.c.directoryExecId(C);
  TEST_ASSERT_TRUE(h.c.onMessage(wrong,1)); TEST_ASSERT_TRUE(h.c.state(C).directory == ActionDirectoryStatus::kFailed);
  TEST_ASSERT_FALSE(h.c.directoryExecId(S).empty());
  TEST_ASSERT_TRUE(h.c.confirm(C,2)); TEST_ASSERT_TRUE(h.c.executionExecId().empty());
  TEST_ASSERT_TRUE(h.c.onMessage(h.page(C),3)); TEST_ASSERT_TRUE(h.c.executionExecId().empty());
  auto late = h.page(S); TEST_ASSERT_FALSE(h.c.onMessage(late,15000));
  TEST_ASSERT_TRUE(h.c.state(S).directory == ActionDirectoryStatus::kFailed);
  TEST_ASSERT_EQUAL(8,h.c.state(C).entries.size());
}
void shared_execution_identity_partial_failure_and_feedback() {
  Harness h; h.ready(); h.c.enter(C,0); h.c.enter(S,0); h.c.onMessage(h.page(C),1); h.c.onMessage(h.page(S),1);
  TEST_ASSERT_TRUE(h.c.confirm(C,2)); const auto id = h.c.executionExecId(); const auto count=h.sent.size();
  TEST_ASSERT_FALSE(h.c.confirm(S,3)); TEST_ASSERT_FALSE(h.c.executeByKey('n',3));
  TEST_ASSERT_EQUAL_STRING(id.c_str(),h.c.executionExecId().c_str()); TEST_ASSERT_EQUAL(count,h.sent.size());
  auto result=h.result(); auto wrong=result; wrong.executedActionId="clipboard.other";
  TEST_ASSERT_FALSE(h.c.onMessage(wrong,4)); wrong=result; wrong.actionType=S; TEST_ASSERT_FALSE(h.c.onMessage(wrong,4));
  TEST_ASSERT_TRUE(h.c.onMessage(result,5)); TEST_ASSERT_TRUE(h.c.execution().feedback.find("已发送粘贴") == 0);
  h.c.tick(3004); TEST_ASSERT_FALSE(h.c.execution().feedback.empty()); h.c.tick(3005); TEST_ASSERT_TRUE(h.c.execution().feedback.empty());
  TEST_ASSERT_TRUE(h.c.confirm(C,3006)); TEST_ASSERT_TRUE(h.c.onMessage(h.result("clipboard_partial_failure"),3007));
  TEST_ASSERT_EQUAL_STRING("粘贴失败，文本已复制",h.c.execution().feedback.c_str());
  TEST_ASSERT_TRUE(h.c.confirm(S,3008)); TEST_ASSERT_FALSE(h.c.confirm(C,3009));
  TEST_ASSERT_TRUE(h.c.onMessage(h.result("script_execute_success"),3010)); TEST_ASSERT_TRUE(h.c.execution().status == ActionExecutionStatus::kSucceeded);
}
void shortcuts_no_cache_timeout_rollover_and_session_reset() {
  Harness h; const uint32_t start=UINT32_MAX-100; h.ready();
  TEST_ASSERT_TRUE(h.c.executeByKey('N',start)); auto old=h.result("clipboard_shortcut_success");
  TEST_ASSERT_TRUE(h.c.onMessage(old,start+1)); TEST_ASSERT_TRUE(h.c.state(C).entries.empty());
  TEST_ASSERT_TRUE(h.c.executeByKey('n',start+2)); auto expired=h.result("clipboard_shortcut_success");
  TEST_ASSERT_FALSE(h.c.onMessage(expired,start+45002u)); TEST_ASSERT_TRUE(h.c.execution().status==ActionExecutionStatus::kUnconfirmed);
  const auto count=h.sent.size(); h.c.tick(start+90000u); TEST_ASSERT_EQUAL(count,h.sent.size());
  h.c.enter(S,start+90001u);h.c.enter(C,start+90001u);h.c.executeByKey('n',start+90001u);
  auto page=h.page(C); old=h.result("clipboard_shortcut_success"); const auto cancellations=h.cancelled.size();
  h.c.disconnect(); TEST_ASSERT_EQUAL(cancellations+3,h.cancelled.size()); h.c.disconnect(); TEST_ASSERT_EQUAL(cancellations+3,h.cancelled.size());
  h.ready(); TEST_ASSERT_TRUE(h.c.execution().feedback.empty()); TEST_ASSERT_TRUE(h.c.executionExecId().empty());
  TEST_ASSERT_FALSE(h.c.onMessage(old,1)); TEST_ASSERT_FALSE(h.c.onMessage(page,1)); TEST_ASSERT_EQUAL(count+3,h.sent.size());
}
void unsupported_types_and_shortcut_outcomes() {
  Harness h; h.c.onSessionReady("pc",true,true,true,{S});h.c.enter(C,0);TEST_ASSERT_TRUE(h.sent.empty());
  TEST_ASSERT_TRUE(h.c.state(C).directory==ActionDirectoryStatus::kUnsupported);
  h.c.executeByKey('n',1); TEST_ASSERT_FALSE(h.c.onMessage(h.result("clipboard_shortcut_success"),2));
  h.c.disconnect();h.ready();
  for (auto name : {"script_shortcut_success","clipboard_shortcut_success","clipboard_unconfirmed","busy","not_found"}) {
    TEST_ASSERT_TRUE(h.c.executeByKey('n',3));auto m=h.result(name);m.actionId=protocol::kShortcutExecuteAction;
    TEST_ASSERT_TRUE(h.c.onMessage(m,4));TEST_ASSERT_TRUE(h.c.executionExecId().empty());
  }
}
int main(int,char**) {
  UNITY_BEGIN(); RUN_TEST(lazy_directories_reverse_order_and_independent_selection);
  RUN_TEST(wrong_type_retry_and_late_directory); RUN_TEST(shared_execution_identity_partial_failure_and_feedback);
  RUN_TEST(shortcuts_no_cache_timeout_rollover_and_session_reset); RUN_TEST(unsupported_types_and_shortcut_outcomes);
  return UNITY_END();
}
