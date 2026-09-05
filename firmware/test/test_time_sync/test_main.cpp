#include <unity.h>
#include <vector>
#include "application/time_sync/time_sync_controller.h"
#include "application/codex/codex_controller.h"
#include "core/protocol_constants.h"

namespace {
constexpr auto timeId=adv::ScheduledTaskId::kSystemTimeSync;
constexpr uint32_t hour=3600000;
struct Clock : adv::SystemClock {
  int64_t utc{0}; bool fail{false};
  bool setUtcMilliseconds(int64_t value) override { if(fail)return false; utc=value; return true; }
  bool readUtcMilliseconds(int64_t& value) const override { value=utc;return true; }
};
struct Fixture {
  adv::ScheduledTaskService scheduler;
  adv::ExecIdGenerator ids;
  Clock clock;
  adv::SystemTimeService time{clock};
  std::vector<std::string> sent,cancelled;
  bool accept{true}; int submissions{0};
  adv::TimeSyncController controller;
  Fixture(uint32_t now=0) : controller(scheduler,ids,time,
      [&](const std::string& id,const std::string& line,uint32_t) {
        ++submissions;
        const auto message=adv::MessageCodec().decode(line);
        TEST_ASSERT_TRUE(message.ok);
        TEST_ASSERT_EQUAL_STRING(adv::protocol::kTimeReadAction,message.message.actionId.c_str());
        if(!accept)return false;
        sent.push_back(id);return true;
      },[&](const std::string& id){cancelled.push_back(id);}) { controller.begin(now); }
  void ready(uint32_t now=0,const std::string& pc="pc",bool supports=true) { controller.onSessionReady(pc,supports,now); }
  void tick(uint32_t now) { controller.tick(now);scheduler.tick(now); }
  adv::Message response(const std::string& code="OK") {
    adv::Message m; m.actionId=adv::protocol::kTimeReadAction; m.execId=controller.inFlightExecId();
    m.resultCode=code; m.epochMilliseconds=1788480000123LL; m.utcOffsetMinutes=480; return m;
  }
  void finish(uint32_t now) { TEST_ASSERT_TRUE(controller.onMessage(response(),now)); }
};
void first_sync_and_success_deadline() {
  Fixture f; f.tick(100);TEST_ASSERT_EQUAL(0,f.sent.size());
  f.ready(200,"pc",false);TEST_ASSERT_EQUAL(0,f.sent.size());
  f.ready(300);TEST_ASSERT_EQUAL(1,f.sent.size());
  f.ready(301);TEST_ASSERT_EQUAL(1,f.sent.size());
  f.finish(500);TEST_ASSERT_TRUE(f.time.hasSynced());
  TEST_ASSERT_EQUAL_INT64(1788480000123LL,f.clock.utc);
  f.tick(hour+499);TEST_ASSERT_EQUAL(1,f.sent.size());
  f.tick(hour+500);TEST_ASSERT_EQUAL(2,f.sent.size());
}
void matching_and_timeout() {
  Fixture f; f.ready(); auto response=f.response();
  response.event=adv::MessageEvent::kRequest;TEST_ASSERT_FALSE(f.controller.onMessage(response,1));
  response=f.response();response.execId="old";TEST_ASSERT_FALSE(f.controller.onMessage(response,1));
  response=f.response();response.actionId=adv::protocol::kCodexUsageAction;TEST_ASSERT_FALSE(f.controller.onMessage(response,1));
  const auto old=f.response();
  f.tick(14999);TEST_ASSERT_FALSE(f.controller.inFlightExecId().empty());
  f.tick(15000);TEST_ASSERT_TRUE(f.controller.inFlightExecId().empty()); TEST_ASSERT_EQUAL(1,f.cancelled.size());
  f.tick(17999);TEST_ASSERT_EQUAL(1,f.sent.size());f.tick(18000);TEST_ASSERT_EQUAL(2,f.sent.size());
  TEST_ASSERT_FALSE(f.controller.onMessage(old,18001));
  f.finish(18002);
}
void bounded_retries_and_next_round() {
  Fixture f;f.ready();
  TEST_ASSERT_TRUE(f.controller.onMessage(f.response("ERROR"),1));
  f.ready(2);f.tick(3000);TEST_ASSERT_EQUAL(1,f.sent.size());
  f.tick(3001);TEST_ASSERT_EQUAL(2,f.sent.size());
  f.controller.onMessage(f.response("BUSY"),3002);
  f.tick(6002);TEST_ASSERT_EQUAL(3,f.sent.size());
  f.controller.onMessage(f.response("ERROR"),6003);
  f.tick(9003);f.tick(hour-1);TEST_ASSERT_EQUAL(3,f.sent.size());
  f.tick(hour);TEST_ASSERT_EQUAL(4,f.sent.size());
}
void rejected_queue_counts_and_reconnect_recovers() {
  Fixture f;f.accept=false;f.ready();TEST_ASSERT_EQUAL(1,f.submissions);
  f.tick(2999);TEST_ASSERT_EQUAL(1,f.submissions);
  f.tick(3000);f.tick(6000);f.tick(9000);TEST_ASSERT_EQUAL(3,f.submissions);
  f.controller.disconnect();f.accept=true;f.ready(10000);TEST_ASSERT_EQUAL(4,f.submissions);
  f.finish(10001);
}
void fresh_reconnect_preserves_deadline_and_expired_reconnect_syncs() {
  Fixture f;f.ready();f.finish(100);
  f.controller.disconnect();f.tick(1000);f.ready(2000);TEST_ASSERT_EQUAL(1,f.sent.size());
  f.tick(hour+99);TEST_ASSERT_EQUAL(1,f.sent.size());
  f.tick(hour+100);TEST_ASSERT_EQUAL(2,f.sent.size());f.finish(hour+200);
  f.controller.disconnect();f.tick(2*hour+200);TEST_ASSERT_EQUAL(2,f.sent.size());
  f.ready(2*hour+201);TEST_ASSERT_EQUAL(3,f.sent.size());
}
void failed_sync_and_computer_switch() {
  Fixture f;f.ready();f.finish(10);f.controller.disconnect();f.ready(20,"other");
  const auto old=f.response();f.clock.fail=true;
  f.controller.onMessage(old,21);
  TEST_ASSERT_EQUAL(10,f.time.lastSuccessfulSyncMs());
  f.controller.disconnect();f.tick(4000);TEST_ASSERT_EQUAL(2,f.sent.size());
  f.clock.fail=false;f.ready(4001,"other");TEST_ASSERT_EQUAL(3,f.sent.size());
  TEST_ASSERT_FALSE(f.controller.onMessage(old,4002));f.finish(4003);
  // Identity change without a transport disconnect also invalidates the prior request.
  f.ready(4004,"third");const auto third=f.response();
  f.ready(4005,"fourth");TEST_ASSERT_FALSE(f.controller.onMessage(third,4006));
  TEST_ASSERT_EQUAL(5,f.sent.size());
}
void capability_loss_and_pause() {
  Fixture f;f.ready();const auto old=f.response();
  f.ready(1,"pc",false);TEST_ASSERT_FALSE(f.controller.onMessage(old,2));
  f.tick(5000);TEST_ASSERT_EQUAL(1,f.sent.size());
  f.ready(5001);TEST_ASSERT_EQUAL(2,f.sent.size());
  f.scheduler.setEnabled(timeId,false,5002);
  f.finish(5003);adv::ScheduledTaskSnapshot snapshot{};f.scheduler.getTask(timeId,snapshot);
  TEST_ASSERT_FALSE(snapshot.enabled);
  f.tick(hour+5003);TEST_ASSERT_EQUAL(2,f.sent.size());
  f.scheduler.setEnabled(timeId,true,hour+5004);
  f.tick(2*hour+5003);TEST_ASSERT_EQUAL(2,f.sent.size());
  f.tick(2*hour+5004);TEST_ASSERT_EQUAL(3,f.sent.size());
  f.controller.onMessage(f.response("ERROR"),2*hour+5005);
  f.scheduler.setEnabled(timeId,false,2*hour+5006);f.tick(2*hour+8005);
  TEST_ASSERT_EQUAL(3,f.sent.size());
}
void wrap_and_long_disconnection() {
  Fixture f(0xffffff00);f.ready(0xffffff00);f.finish(0xffffff10);
  f.controller.disconnect();
  // Keep ticking across a full 32-bit revolution; expiry stays latched.
  uint32_t now=0xffffff10;
  for(int i=0;i<1200;++i){now+=hour;f.tick(now);}
  f.ready(now+1);TEST_ASSERT_EQUAL(2,f.sent.size());
  Fixture timeout(0xffffff00);timeout.ready(0xffffff00);
  timeout.tick(static_cast<uint32_t>(0xffffff00u+15000u));
  TEST_ASSERT_TRUE(timeout.controller.inFlightExecId().empty());
  timeout.tick(static_cast<uint32_t>(0xffffff00u+18000u));TEST_ASSERT_EQUAL(2,timeout.sent.size());
}
void utc_jumps_do_not_schedule_and_shared_ids_are_unique() {
  Fixture f;f.ready();f.finish(10);
  f.clock.utc+=999999999;f.tick(100);TEST_ASSERT_EQUAL(1,f.sent.size());
  f.clock.utc=1;f.tick(200);TEST_ASSERT_EQUAL(1,f.sent.size());
  std::string codexId;
  adv::CodexController codex(f.scheduler,f.ids,
      [&](const std::string& id,const std::string&,uint32_t){codexId=id;return true;},[](const std::string&){});
  codex.begin(200);codex.onSessionReady(true,200);
  TEST_ASSERT_TRUE(codexId!=f.sent[0]);
  f.tick(hour+10);TEST_ASSERT_TRUE(f.sent.back()!=codexId);
}
}
int main(int,char**) {
  UNITY_BEGIN();
  RUN_TEST(first_sync_and_success_deadline);RUN_TEST(matching_and_timeout);
  RUN_TEST(bounded_retries_and_next_round);RUN_TEST(rejected_queue_counts_and_reconnect_recovers);
  RUN_TEST(fresh_reconnect_preserves_deadline_and_expired_reconnect_syncs);
  RUN_TEST(failed_sync_and_computer_switch);RUN_TEST(capability_loss_and_pause);
  RUN_TEST(wrap_and_long_disconnection);RUN_TEST(utc_jumps_do_not_schedule_and_shared_ids_are_unique);
  return UNITY_END();
}
