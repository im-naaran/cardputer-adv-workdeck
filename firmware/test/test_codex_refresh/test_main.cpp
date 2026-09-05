#include <unity.h>
#include <vector>
#include "application/codex/codex_controller.h"
#include "core/protocol_constants.h"
namespace {
adv::Message response(const std::string& id, const std::string& code="OK") {
  adv::Message m; m.actionId=adv::protocol::kCodexUsageAction; m.execId=id; m.resultCode=code;
  if(code=="OK") { m.fetchedAtEpochSeconds=1000; m.windows.push_back({"codex","","primary",42,true,300,true,2000}); }
  return m;
}
struct Fixture {
  adv::ScheduledTaskService scheduler;
  adv::ExecIdGenerator ids;
  std::vector<std::string> sent, cancelled;
  bool accept{true};
  adv::CodexController controller;
  Fixture(uint32_t now=0) : controller(scheduler,ids,
      [&](const std::string& id,const std::string&,uint32_t){if(!accept)return false;sent.push_back(id);return true;},
      [&](const std::string& id){cancelled.push_back(id);}) { controller.begin(now); }
  void ready(uint32_t seconds=300,uint32_t now=0,bool supported=true) { // Short scheduler intervals keep these controller boundary fixtures compact.
    if(controller.taskState().intervalMs!=seconds*1000)
      scheduler.updateInterval(adv::ScheduledTaskId::kCodexUsageRefresh,seconds*1000,now);
    controller.onSessionReady(supported,now); }
  void tick(uint32_t now) { controller.tick(now); scheduler.tick(now); }
  void finish(uint32_t now,const std::string& code="OK") { controller.onMessage(response(controller.state().inFlightExecId(),code),now); }
  void key(uint32_t now,bool fn=false) { controller.onKey({adv::Key::kEnter,fn},now); }
};
void hello_and_background_interval() {
  Fixture f; f.ready(1); TEST_ASSERT_EQUAL(1,f.sent.size());
  f.key(1); f.tick(900); TEST_ASSERT_EQUAL(1,f.sent.size());
  f.finish(500); f.controller.onPageChanged(false);
  f.tick(999); TEST_ASSERT_EQUAL(1,f.sent.size());
  f.tick(1000); TEST_ASSERT_EQUAL(2,f.sent.size()); // anchored to submission, not response at 500
  f.finish(1010); f.controller.onPageChanged(true); TEST_ASSERT_EQUAL(2,f.sent.size());
  f.ready(1,1200); f.tick(1999); TEST_ASSERT_EQUAL(2,f.sent.size());
  f.tick(2000); TEST_ASSERT_EQUAL(3,f.sent.size());
}
void pause_manual_and_resume() {
  Fixture f; f.ready(1); f.key(10,true);
  TEST_ASSERT_FALSE(f.controller.taskState().enabled); TEST_ASSERT_EQUAL(1,f.sent.size());
  f.finish(20); TEST_ASSERT_TRUE(f.controller.state().hasData());
  f.tick(3000); TEST_ASSERT_EQUAL(1,f.sent.size());
  f.key(3100); TEST_ASSERT_EQUAL(2,f.sent.size()); f.finish(3110);
  f.controller.disconnect(); f.ready(1,3200); TEST_ASSERT_EQUAL(2,f.sent.size());
  f.key(3300,true); f.tick(4299); TEST_ASSERT_EQUAL(2,f.sent.size());
  f.tick(4300); TEST_ASSERT_EQUAL(3,f.sent.size());
}
void manual_submission_anchor_and_rejection() {
  Fixture f; f.ready(1); f.finish(10);
  f.accept=false; f.key(900); f.accept=true;
  f.tick(1000); TEST_ASSERT_EQUAL(2,f.sent.size()); f.finish(1010);
  f.key(1900); f.finish(1910);
  f.tick(2000); TEST_ASSERT_EQUAL(3,f.sent.size());
  f.tick(2900); TEST_ASSERT_EQUAL(4,f.sent.size());
}
void timeout_late_response_and_cache_age() {
  Fixture f; f.ready(); f.finish(100); f.key(200);
  const auto expired=f.controller.state().inFlightExecId();
  auto wrong=response(expired); wrong.event=adv::MessageEvent::kRequest;
  TEST_ASSERT_FALSE(f.controller.onMessage(wrong,201));
  wrong=response(expired); wrong.actionId=adv::protocol::kTimeReadAction;
  TEST_ASSERT_FALSE(f.controller.onMessage(wrong,201));
  auto empty=response(expired); empty.windows.clear();
  TEST_ASSERT_FALSE(f.controller.onMessage(empty,201));
  f.tick(210199); TEST_ASSERT_TRUE(f.controller.state().inFlight());
  f.tick(210200); TEST_ASSERT_FALSE(f.controller.state().inFlight());
  TEST_ASSERT_EQUAL_STRING(expired.c_str(),f.cancelled.back().c_str());
  TEST_ASSERT_EQUAL(100,f.controller.state().receivedAtMs());
  TEST_ASSERT_EQUAL_STRING("TIMEOUT",f.controller.state().lastErrorCode().c_str());
  f.key(210201); const auto fresh=f.controller.state().inFlightExecId();
  TEST_ASSERT_TRUE(expired!=fresh);
  TEST_ASSERT_FALSE(f.controller.onMessage(response(expired),210202));
  f.finish(210203,"BUSY"); TEST_ASSERT_EQUAL(100,f.controller.state().receivedAtMs());
  TEST_ASSERT_TRUE(f.controller.state().hasData());
}
void capability_disconnect_and_wrap() {
  Fixture f(0xffffff00); f.ready(1,0xffffff00,false); f.tick(744);
  TEST_ASSERT_EQUAL(0,f.sent.size()); f.key(750); TEST_ASSERT_EQUAL(0,f.sent.size());
  f.ready(1,800,true); TEST_ASSERT_EQUAL(1,f.sent.size()); const auto old=f.sent.back();
  f.controller.disconnect(); TEST_ASSERT_FALSE(f.controller.state().inFlight());
  TEST_ASSERT_FALSE(f.controller.state().hasData()); f.ready(1,900,true);
  TEST_ASSERT_TRUE(f.sent.back()!=old); f.finish(950);
  f.ready(1,1000,false); f.tick(2000); TEST_ASSERT_EQUAL(2,f.sent.size());
  Fixture wrap(0xffffff00); wrap.ready(1,0xffffff00); wrap.finish(0xffffff10);
  wrap.tick(743); TEST_ASSERT_EQUAL(1,wrap.sent.size());
  wrap.tick(744); TEST_ASSERT_EQUAL(2,wrap.sent.size());
}
void skipped_cycles_do_not_catch_up() {
  Fixture f; f.ready(1); f.tick(5000); TEST_ASSERT_EQUAL(1,f.sent.size());
  f.finish(5010); f.tick(5011); TEST_ASSERT_EQUAL(1,f.sent.size());
  f.tick(6000); TEST_ASSERT_EQUAL(2,f.sent.size());
  f.finish(6010); f.ready(2,6100);
  f.tick(8099); TEST_ASSERT_EQUAL(2,f.sent.size()); f.tick(8100); TEST_ASSERT_EQUAL(3,f.sent.size());
}
}
int main(int,char**) {
  UNITY_BEGIN();
  RUN_TEST(hello_and_background_interval); RUN_TEST(pause_manual_and_resume);
  RUN_TEST(manual_submission_anchor_and_rejection); RUN_TEST(timeout_late_response_and_cache_age);
  RUN_TEST(capability_disconnect_and_wrap); RUN_TEST(skipped_cycles_do_not_catch_up);
  return UNITY_END();
}
