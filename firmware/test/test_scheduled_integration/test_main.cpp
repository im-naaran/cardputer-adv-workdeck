#include <unity.h>
#include <vector>
#include "application/codex/codex_controller.h"
#include "application/time_sync/time_sync_controller.h"
#include "core/connection_session.h"
#include "core/message_router.h"
#include "core/outgoing_jsonl_queue.h"
#include "core/protocol_constants.h"
#include "platform/ble_transport.h"

namespace {
struct Clock : adv::SystemClock {
  int64_t utc{0};
  bool setUtcMilliseconds(int64_t value) override { utc = value; return true; }
  bool readUtcMilliseconds(int64_t& value) const override { value = utc; return true; }
};
struct ConfigStore : adv::ConfigFileStore {
  std::string bytes=adv::encodeCodexConfig({3600});
  adv::ConfigStatus read(const std::string&, std::string& out) override { out=bytes;return adv::ConfigStatus::kOk; }
  adv::ConfigStatus replace(const std::string&, const std::string& value) override { bytes=value;return adv::ConfigStatus::kOk; }
};
struct Fixture {
  adv::ScheduledTaskService scheduler;
  adv::ExecIdGenerator ids;
  Clock clock;
  adv::SystemTimeService time{clock};
  adv::ConnectionSession session;
  adv::MessageRouter router;
  adv::OutgoingJsonlQueue queue;
  adv::BleTransport ble;
  uint32_t now{0};
  std::string wire;
  std::vector<adv::Message> submitted;
  int displays{0};
  adv::TimeSyncController sync;
  adv::CodexController codex;
  ConfigStore store;
  adv::CodexConfigService config{store,codex,[this]{return now;}};
  Fixture(uint32_t start = 0)
      : now(start), sync(scheduler, ids, time,
          [&](const auto& id, const auto& line, uint32_t t) { return send(id, line, t); },
          [&](const auto& id) { queue.cancelPending(id); }),
        codex(scheduler, ids,
          [&](const auto& id, const auto& line, uint32_t t) { return send(id, line, t); },
          [&](const auto& id) { queue.cancelPending(id); }) {
    sync.begin(now); codex.begin(now); config.reload();
    scheduler.registerTask({adv::ScheduledTaskId::kDisplayRefresh,60000,true,false},
                          [&](uint32_t) { ++displays; },now);
    router.registerHandler(adv::protocol::kHelloAction, [&](const adv::Message& m) {
      const bool ready = session.ready();
      const auto previous = session.computerId();
      if (!session.acceptHello(m)) { sync.disconnect(); codex.disconnect(); return; }
      if (ready && previous != session.computerId()) { sync.disconnect(); codex.disconnect(); }
      sync.onSessionReady(session.computerId(), session.supports(adv::protocol::kTimeReadAction), now);
      codex.onSessionReady(session.supports(adv::protocol::kCodexUsageAction), now);
    });
    router.registerHandler(adv::protocol::kTimeReadAction, [&](const auto& m) { sync.onMessage(m,now); });
    router.registerHandler(adv::protocol::kCodexUsageAction, [&](const auto& m) { codex.onMessage(m,now); });
    ble.onConnected(); receive(now);
  }
  bool send(const std::string& id, const std::string& line, uint32_t t) {
    if (!queue.enqueue(id,line,t,ble.generation())) return false;
    const auto decoded = adv::MessageCodec().decode(line);
    TEST_ASSERT_TRUE(decoded.ok); submitted.push_back(decoded.message); return true;
  }
  void receive(uint32_t t) {
    now = t; ble.poll(now);
    if (ble.consumeConnectionChanged()) {
      queue.clear(); sync.disconnect(); codex.disconnect();
      if (ble.connected()) session.onBleConnected(); else session.disconnect();
    }
    std::string line;
    while (ble.takeMessage(line)) {
      const auto decoded = adv::MessageCodec().decode(line);
      if (decoded.ok) router.route(decoded.message);
    }
  }
  void tick(uint32_t t) {
    receive(t);
    // Match main: responses first, fresh monotonic time, short timers, tasks, one fragment.
    sync.tick(now); codex.tick(now); scheduler.tick(now);
    queue.poll(now,ble.generation(),[&](const std::string& chunk) { wire += chunk; return true; });
  }
  void drain(uint32_t t) { for (unsigned i=0;i<100 && queue.size();++i) tick(t+i*10); }
  void hello(uint32_t t, const std::string& pc="pc", bool timeCap=true, bool codexCap=true) {
    now=t; adv::Message m; m.actionId=adv::protocol::kHelloAction;
    m.resultCode="OK"; m.protocolVersion=adv::protocol::kVersion; m.computerId=pc;
    if(timeCap)m.capabilities.push_back(adv::protocol::kTimeReadAction);
    if(codexCap)m.capabilities.push_back(adv::protocol::kCodexUsageAction);
    router.route(m);
  }
  adv::Message timeResponse() {
    adv::Message m; m.actionId=adv::protocol::kTimeReadAction; m.execId=sync.inFlightExecId();
    m.resultCode="OK"; m.epochMilliseconds=1788480000123LL; m.utcOffsetMinutes=480; return m;
  }
  void finishTime(uint32_t t) { now=t; router.route(timeResponse()); }
  void finishCodex(uint32_t t) {
    now=t; adv::Message m; m.actionId=adv::protocol::kCodexUsageAction;
    m.execId=codex.state().inFlightExecId(); m.resultCode="OK"; m.windows.push_back({});
    router.route(m);
  }
};
void simultaneous_tasks_and_slow_codex() {
  Fixture f; f.hello(0); f.drain(0);
  TEST_ASSERT_EQUAL(2,f.submitted.size());
  TEST_ASSERT_EQUAL_STRING(adv::protocol::kTimeReadAction,f.submitted[0].actionId.c_str());
  TEST_ASSERT_TRUE(f.submitted[0].execId != f.submitted[1].execId);
  // Decoding whole wire lines detects fragment interleaving, not only enqueue order.
  const auto split=f.wire.find('\n');
  TEST_ASSERT_TRUE(adv::MessageCodec().decode(f.wire.substr(0,split)).ok);
  TEST_ASSERT_TRUE(adv::MessageCodec().decode(f.wire.substr(split+1)).ok);
  f.finishTime(1000); TEST_ASSERT_TRUE(f.codex.state().inFlight());
  TEST_ASSERT_TRUE(f.time.hasSynced()); f.finishCodex(1000);
  f.scheduler.rescheduleFromNow(adv::ScheduledTaskId::kCodexUsageRefresh,1000);
  f.tick(3600999); TEST_ASSERT_EQUAL(2,f.submitted.size());
  f.tick(3601000); TEST_ASSERT_EQUAL(4,f.submitted.size());
  TEST_ASSERT_EQUAL_STRING(adv::protocol::kTimeReadAction,f.submitted[2].actionId.c_str());
  TEST_ASSERT_TRUE(f.submitted[2].execId != f.submitted[3].execId);
  TEST_ASSERT_TRUE(f.displays>0);
}
void hello_changes_pause_and_background() {
  Fixture f; f.hello(0); f.drain(0); f.finishTime(1000); f.finishCodex(1000);
  f.hello(2000); TEST_ASSERT_EQUAL(2,f.submitted.size());
  f.codex.onPageChanged(true); f.codex.onKey({adv::Key::kEnter,true},2000);
  f.codex.applyConfig({60},3000); f.hello(3000,"pc",true,true);
  TEST_ASSERT_FALSE(f.codex.taskState().enabled); TEST_ASSERT_EQUAL(60000,f.codex.taskState().intervalMs);
  f.tick(123000); TEST_ASSERT_EQUAL(2,f.submitted.size());
  f.codex.onKey({adv::Key::kEnter,false},123001); TEST_ASSERT_EQUAL(3,f.submitted.size());
  f.drain(123010); f.finishCodex(124000);
  f.codex.onKey({adv::Key::kEnter,true},124000);
  f.codex.onPageChanged(false);
  f.hello(125000,"pc",true,true); // Identical hello does not reset the resume deadline.
  f.hello(126000,"pc",true,true);
  f.tick(183999); TEST_ASSERT_EQUAL(3,f.submitted.size());
  f.tick(184000); TEST_ASSERT_TRUE(f.codex.state().inFlight());
  f.hello(184001,"pc",false,false);
  TEST_ASSERT_FALSE(f.codex.state().inFlight()); TEST_ASSERT_TRUE(f.sync.inFlightExecId().empty());
  const auto count=f.submitted.size(); f.hello(184002);
  TEST_ASSERT_TRUE(f.submitted.size()>count);
}
void identity_invalid_hello_and_fast_reconnect() {
  Fixture f; f.hello(0); const auto old=f.timeResponse();
  f.hello(1,"other"); TEST_ASSERT_EQUAL(4,f.submitted.size());
  TEST_ASSERT_FALSE(f.sync.onMessage(old,2));
  adv::Message bad; bad.actionId=adv::protocol::kHelloAction;
  f.router.route(bad); TEST_ASSERT_FALSE(f.session.ready());
  TEST_ASSERT_TRUE(f.sync.inFlightExecId().empty()); TEST_ASSERT_FALSE(f.codex.state().inFlight());
  f.hello(3); const auto before=f.timeResponse();
  const std::string partial="{\"event\":";
  f.ble.onWriteBytes(reinterpret_cast<const uint8_t*>(partial.data()),partial.size()); f.receive(4);
  f.ble.onDisconnected(); f.ble.onConnected(); f.receive(5);
  TEST_ASSERT_FALSE(f.session.ready()); TEST_ASSERT_EQUAL(0,f.queue.size());
  f.hello(6); TEST_ASSERT_FALSE(f.sync.onMessage(before,7));
  TEST_ASSERT_TRUE(f.sync.inFlightExecId()!=before.execId);
}
void queue_expiry_partial_cancel_and_recovery() {
  Fixture f;
  for(int i=0;i<4;++i)f.queue.enqueue(std::to_string(i),"{}",0,f.ble.generation());
  f.hello(0); TEST_ASSERT_EQUAL(0,f.submitted.size());
  f.tick(10001); TEST_ASSERT_EQUAL(0,f.queue.size());
  f.tick(13001); TEST_ASSERT_EQUAL(1,f.submitted.size());
  f.drain(13011); f.finishTime(14000);
  f.codex.onPageChanged(true); f.codex.onKey({adv::Key::kEnter,false},14000);
  f.tick(14001); const auto old=f.codex.state().inFlightExecId();
  const auto prefix=f.wire.size();
  f.tick(224000); TEST_ASSERT_FALSE(f.codex.state().inFlight());
  // Timeout cancellation cannot truncate a started JSON frame; it must finish to LF.
  f.drain(224010); TEST_ASSERT_TRUE(f.wire.size()>prefix); TEST_ASSERT_EQUAL('\n',f.wire.back());
  const auto lastStart=f.wire.rfind('\n',f.wire.size()-2);
  TEST_ASSERT_TRUE(adv::MessageCodec().decode(f.wire.substr(lastStart+1)).ok);
  f.codex.onKey({adv::Key::kEnter,false},225000);
  TEST_ASSERT_TRUE(f.codex.state().inFlightExecId()!=old);
}
void runtime_config_and_legacy_hello() {
  Fixture f; f.hello(0);f.drain(0);f.finishTime(1000);f.finishCodex(1000);
  f.now=2000;
  TEST_ASSERT_TRUE(f.config.save(adv::encodeCodexConfig({60})).changed);
  // Old desktop values stay in the wire envelope, but never enter the local task state.
  const auto legacy=adv::MessageCodec().decode(R"({"event":"response","actionId":"system.hello","execId":"hello","result":{"code":"OK","msg":"ready","data":{"protocolVersion":2,"supportedActionTypes":[],"computerId":"pc","computerName":"PC","capabilities":["codex.usage.read","system.time.read"],"settings":{"codexRefreshIntervalSeconds":1}}}})");
  TEST_ASSERT_TRUE(legacy.ok);
  f.now=3000;f.router.route(legacy.message);
  TEST_ASSERT_EQUAL(60000,f.codex.taskState().intervalMs);
  f.tick(61999);TEST_ASSERT_EQUAL(2,f.submitted.size());
  f.tick(62000);TEST_ASSERT_EQUAL(3,f.submitted.size());
  TEST_ASSERT_TRUE(f.time.hasSynced());
  f.drain(62010);f.finishCodex(63000);
  f.codex.onKey({adv::Key::kEnter,true},63000);
  f.now=64000;TEST_ASSERT_TRUE(f.config.save(adv::encodeCodexConfig({300})).changed);
  TEST_ASSERT_FALSE(f.codex.taskState().enabled);
  f.ble.onDisconnected();f.ble.onConnected();f.receive(65000);f.hello(65000);
  TEST_ASSERT_EQUAL(300000,f.codex.taskState().intervalMs);
  TEST_ASSERT_FALSE(f.codex.taskState().enabled);
}
void wrap_and_calendar_jumps() {
  const uint32_t start=0xfffff000;
  Fixture f(start); f.hello(start); f.drain(start); f.finishTime(start+1000); f.finishCodex(start+1000);
  f.clock.utc=1; f.tick(start+2000); TEST_ASSERT_EQUAL(2,f.submitted.size());
  f.clock.utc=9999999999999LL; f.tick(start+3599999); TEST_ASSERT_EQUAL(2,f.submitted.size());
  f.tick(start+3600000); TEST_ASSERT_EQUAL(3,f.submitted.size());
  f.tick(start+3601000); TEST_ASSERT_EQUAL(4,f.submitted.size());
}
}
int main(int,char**) {
  UNITY_BEGIN(); RUN_TEST(simultaneous_tasks_and_slow_codex);
  RUN_TEST(hello_changes_pause_and_background); RUN_TEST(identity_invalid_hello_and_fast_reconnect);
  RUN_TEST(queue_expiry_partial_cancel_and_recovery); RUN_TEST(wrap_and_calendar_jumps);RUN_TEST(runtime_config_and_legacy_hello);
  return UNITY_END();
}
