#include <unity.h>
#include "application/codex/codex_page.h"
#include "application/codex/codex_controller.h"
#include "application/time_sync/time_sync_controller.h"
#include "application/actions/action_list_page.h"
#include "application/input/input_config_commands.h"
#include "core/message_router.h"
#include "core/connection_session.h"
#include "core/outgoing_jsonl_queue.h"
#include "core/protocol_constants.h"
using namespace adv;
struct Clock : SystemClock {
  int64_t utc=0;
  bool setUtcMilliseconds(int64_t value) override {utc=value;return true;}
  bool readUtcMilliseconds(int64_t& out) const override {out=utc;return true;}
};
// Equivalent loop harness: real routing/controllers/queue, fake BLE sink and clock.
struct App {
  ScheduledTaskService scheduler;ExecIdGenerator ids;Clock clock;SystemTimeService time{clock};
  ConnectionSession session;MessageRouter messages;OutgoingJsonlQueue queue;
  NavigationService navigation;InputRouter input;KeyPressTracker keys;CodexPage codexPage;ActionListPage scriptsPage{ActionType::kScript};
  uint32_t now=0;bool dirty=false;
  std::vector<Message> sent;
  bool send(const std::string& id,const std::string& bytes,uint32_t t) {
    if(!queue.enqueue(id,bytes,t,1))return false;
    auto decoded=MessageCodec{}.decode(bytes);TEST_ASSERT_TRUE(decoded.ok);sent.push_back(decoded.message);return true;
  }
  TimeSyncController sync{scheduler,ids,time,[&](const auto& id,const auto& b,uint32_t t){return send(id,b,t);},[&](const auto& id){queue.cancelPending(id);}};
  CodexController codex{scheduler,ids,[&](const auto& id,const auto& b,uint32_t t){return send(id,b,t);},[&](const auto& id){queue.cancelPending(id);}};
  ActionsController scripts{ids,[&](const auto& id,const auto& b,uint32_t t){return send(id,b,t);},[&](const auto& id){queue.cancelPending(id);}};
  App() {
    sync.begin(0);codex.begin(0);session.onBleConnected();
    messages.registerHandler(protocol::kHelloAction,[&](const Message& m){
      auto previous=session.computerId();bool ready=session.ready();
      if(!session.acceptHello(m)){sync.disconnect();codex.disconnect();scripts.disconnect();scriptsPage.reset();return;}
      if(ready&&previous!=session.computerId()){sync.disconnect();codex.disconnect();scripts.disconnect();scriptsPage.reset();}
      sync.onSessionReady(session.computerId(),session.supports(protocol::kTimeReadAction),now);
      codex.onSessionReady(session.supports(protocol::kCodexUsageAction),now);
      scripts.onSessionReady(session.computerId(),session.supports(protocol::kActionsListAction),session.supports(protocol::kActionsExecuteAction),session.supports(protocol::kShortcutExecuteAction),{ActionType::kScript});
      // This legacy regression fixture explicitly opens the script directory.
      scripts.enter(ActionType::kScript,now);
    });
    messages.registerHandler(protocol::kTimeReadAction,[&](const auto& m){sync.onMessage(m,now);});
    messages.registerHandler(protocol::kCodexUsageAction,[&](const auto& m){codex.onMessage(m,now);});
    for(const auto* action:{protocol::kActionsListAction,protocol::kActionsExecuteAction,protocol::kShortcutExecuteAction})
      messages.registerHandler(action,[&](const auto& m){scripts.onMessage(m,now);});
  }
  void hello(bool scriptSupport=true,std::string pc="pc") {
    Message m;m.actionId=protocol::kHelloAction;m.resultCode="OK";m.protocolVersion=protocol::kVersion;m.computerId=pc;m.computerName="PC";
    m.capabilities={protocol::kTimeReadAction,protocol::kCodexUsageAction};
    if(scriptSupport)m.capabilities.insert(m.capabilities.end(),{protocol::kActionsListAction,protocol::kActionsExecuteAction,protocol::kShortcutExecuteAction});
    messages.route(m);
  }
  void key(KeyEvent event) {
    const auto r=input.route(event,navigation.current());
    if(r.action==InputAction::kNavigation){navigation.handleGlobal(r.event);codex.onPageChanged(navigation.current()==Module::kCodex);return;}
    if(r.action==InputAction::kShortcut){scripts.executeByKey(r.event.character,now);return;}
    if(!session.ready())return;
    if(navigation.current()==Module::kCodex) {
      if(r.action==InputAction::kConfirm||r.action==InputAction::kCodexToggle)codex.onKey(r.event,now);
      if(r.action==InputAction::kDirection&&r.event.key==Key::kDown)codexPage.scroll(1,codex.state().windows().size());
      if(r.action==InputAction::kDirection&&r.event.key==Key::kUp)codexPage.scroll(-1,codex.state().windows().size());
    } else if(navigation.current()==Module::kScripts) {
      if(r.action==InputAction::kConfirm)scripts.confirm(ActionType::kScript,now);
      if(r.action==InputAction::kDirection&&r.event.key==Key::kDown)scripts.moveSelection(ActionType::kScript,1,now);
      if(r.action==InputAction::kDirection&&r.event.key==Key::kUp)scripts.moveSelection(ActionType::kScript,-1,now);
    }
  }
  void scan(const InputSnapshot& snapshot){for(auto e:keys.update(snapshot))key(e);}
  void drain(){for(int i=0;queue.size()&&i<1000;++i){queue.poll(now,1,[](const auto&){return true;});now+=10;}TEST_ASSERT_EQUAL(0,queue.size());}
  void page(bool success=true) {
    Message m;m.actionId=protocol::kActionsListAction;m.execId=scripts.directoryExecId(ActionType::kScript);m.resultCode=success?"OK":"ERROR";
    m.hasActionType=true;m.total=2;m.actions={{"script.first","第一项","g","g"},{"script.second","第二项","g",""}};messages.route(m);
  }
  void finishScript() {
    Message m;m.actionId=sent.back().actionId;m.execId=scripts.executionExecId();m.resultCode="OK";
    m.hasActionType=true;m.executedActionId="script.first";m.executedName="第一项";m.hasExitCode=true;messages.route(m);
  }
  void tick(uint32_t t) {
    now=t;const auto d=scripts.state(ActionType::kScript).directory;const auto e=scripts.execution().status;bool feedback=!scripts.execution().feedback.empty();
    scripts.tick(now);dirty=d!=scripts.state(ActionType::kScript).directory||e!=scripts.execution().status||feedback!=!scripts.execution().feedback.empty();
    sync.tick(now);codex.tick(now);scheduler.tick(now);
  }
  void disconnect(){queue.clear();sync.disconnect();codex.disconnect();scripts.disconnect();scriptsPage.reset();session.disconnect();}
};
void hello_order_and_independent_background() {
  App app;app.hello();TEST_ASSERT_EQUAL(3,app.sent.size());
  TEST_ASSERT_EQUAL_STRING(protocol::kTimeReadAction,app.sent[0].actionId.c_str());
  TEST_ASSERT_EQUAL_STRING(protocol::kCodexUsageAction,app.sent[1].actionId.c_str());
  TEST_ASSERT_EQUAL_STRING(protocol::kActionsListAction,app.sent[2].actionId.c_str());
  app.hello();TEST_ASSERT_EQUAL(3,app.sent.size());app.drain();app.page(false);
  app.key({Key::kCharacter,false,'g',true});TEST_ASSERT_EQUAL(4,app.sent.size());
  TEST_ASSERT_TRUE(app.codex.state().inFlight());TEST_ASSERT_FALSE(app.sync.inFlightExecId().empty());
  Message time;time.actionId=protocol::kTimeReadAction;time.execId=app.sync.inFlightExecId();time.resultCode="OK";
  time.epochMilliseconds=1788652800000;time.utcOffsetMinutes=480;app.messages.route(time);TEST_ASSERT_TRUE(app.time.hasSynced());
  Message usage;usage.actionId=protocol::kCodexUsageAction;usage.execId=app.codex.state().inFlightExecId();usage.resultCode="OK";usage.windows.resize(4);
  app.messages.route(usage);TEST_ASSERT_FALSE(app.codex.state().inFlight());
  TEST_ASSERT_FALSE(app.scripts.executionExecId().empty());
  app.key({Key::kEnter,false});TEST_ASSERT_TRUE(app.codex.state().inFlight());
}
void four_pages_keys_and_feedback_expiration() {
  App app;app.hello();app.drain();app.page();
  const Key modules[]={Key::kDigit1,Key::kDigit2,Key::kDigit3,Key::kDigit4};
  for(int m=0;m<4;++m) {
    app.key({modules[m],true});const auto before=app.sent.size();
    app.key({Key::kCharacter,false,'g'});TEST_ASSERT_EQUAL(before,app.sent.size());
    app.key({Key::kCharacter,true,'g',true});TEST_ASSERT_EQUAL(before,app.sent.size());
    app.key({Key::kCharacter,false,'g',true});TEST_ASSERT_EQUAL(before+1,app.sent.size());
    app.key({Key::kCharacter,false,'g',true});TEST_ASSERT_EQUAL(before+1,app.sent.size());
    TEST_ASSERT_TRUE(app.scripts.execution().feedback.find("请稍候")!=std::string::npos);
    app.key({modules[(m+1)%4],true});TEST_ASSERT_FALSE(app.scripts.executionExecId().empty());
    app.drain();app.finishScript();const auto finished=app.now;
    TEST_ASSERT_FALSE(app.scripts.execution().feedback.empty());
    app.tick(finished+2999);TEST_ASSERT_FALSE(app.dirty);TEST_ASSERT_FALSE(app.scripts.execution().feedback.empty());
    app.tick(finished+3000);TEST_ASSERT_TRUE(app.dirty);TEST_ASSERT_TRUE(app.scripts.execution().feedback.empty());
    app.tick(finished+3001);TEST_ASSERT_FALSE(app.dirty);
  }
}
void fn_enter_and_mapping_config() {
  App app;app.hello();app.drain();app.page();
  Message usage;usage.actionId=protocol::kCodexUsageAction;usage.execId=app.codex.state().inFlightExecId();usage.resultCode="OK";usage.windows.resize(4);app.messages.route(usage);
  app.key({Key::kCharacter,false,'.'});TEST_ASSERT_EQUAL(1,app.codexPage.scrollOffset());
  InputConfig config;config.directionMapping[1]=false;app.input.applyConfig(config);
  app.key({Key::kDigit2,true});app.key({Key::kCharacter,false,'.'});TEST_ASSERT_EQUAL(0,app.scripts.state(ActionType::kScript).selected);
  const bool automatic=app.codex.taskState().enabled;app.key({Key::kEnter,true});TEST_ASSERT_EQUAL(automatic,app.codex.taskState().enabled);
  app.input.applyConfig({});app.key({Key::kCharacter,false,'.'});TEST_ASSERT_EQUAL(1,app.scripts.state(ActionType::kScript).selected);
  app.key({Key::kEnter,false});TEST_ASSERT_FALSE(app.scripts.executionExecId().empty());
  app.key({Key::kDigit1,true});app.key({Key::kEnter,true});TEST_ASSERT_FALSE(app.codex.taskState().enabled);
  app.key({Key::kEnter,true,0,true});TEST_ASSERT_FALSE(app.codex.taskState().enabled);
  app.key({Key::kDigit2,true});TEST_ASSERT_EQUAL(1,app.scripts.state(ActionType::kScript).selected);
}
void physical_hold_and_disconnect() {
  App app;app.hello();app.drain();app.page();InputSnapshot s;s.alt=true;s.pressed['g']=true;
  app.scan(s);const auto count=app.sent.size();app.scan(s);s.fn=true;app.scan(s);s.fn=false;app.scan(s);
  TEST_ASSERT_EQUAL(count,app.sent.size());app.drain();app.finishScript();app.scan(s);TEST_ASSERT_EQUAL(count,app.sent.size());
  s.pressed['g']=false;app.scan(s);s.pressed['g']=true;app.scan(s);TEST_ASSERT_EQUAL(count+1,app.sent.size());
  const auto old=app.scripts.executionExecId();app.disconnect();TEST_ASSERT_TRUE(app.scripts.execution().feedback.empty());
  app.key({Key::kDigit4,true});TEST_ASSERT_EQUAL_INT((int)Module::kSettings,(int)app.navigation.current());
  app.key({Key::kCharacter,false,'g',true});TEST_ASSERT_EQUAL(count+1,app.sent.size());
  TEST_ASSERT_TRUE(app.scripts.execution().feedback.find("不可用")!=std::string::npos);
  app.session.onBleConnected();app.hello();TEST_ASSERT_TRUE(app.scripts.executionExecId().empty());
  Message late;late.actionId=protocol::kShortcutExecuteAction;late.execId=old;late.resultCode="OK";
  app.messages.route(late);TEST_ASSERT_TRUE(app.scripts.execution().feedback.empty());
}
void old_hello_and_invalid_renegotiation() {
  App app;app.hello(false);TEST_ASSERT_EQUAL(2,app.sent.size());app.drain();
  app.key({Key::kCharacter,false,'g',true});TEST_ASSERT_EQUAL(2,app.sent.size());
  app.hello();TEST_ASSERT_EQUAL(3,app.sent.size());app.drain();app.page();
  app.key({Key::kCharacter,false,'g',true});app.drain();
  app.hello(true,"other");TEST_ASSERT_TRUE(app.scripts.state(ActionType::kScript).entries.empty());TEST_ASSERT_TRUE(app.scripts.executionExecId().empty());
  Message invalid;invalid.actionId=protocol::kHelloAction;app.messages.route(invalid);
  TEST_ASSERT_FALSE(app.session.ready());TEST_ASSERT_TRUE(app.scripts.directoryExecId(ActionType::kScript).empty());
  TEST_ASSERT_FALSE(app.codex.state().inFlight());TEST_ASSERT_TRUE(app.sync.inFlightExecId().empty());
}
int main(int,char**) {UNITY_BEGIN();RUN_TEST(hello_order_and_independent_background);RUN_TEST(four_pages_keys_and_feedback_expiration);
RUN_TEST(fn_enter_and_mapping_config);RUN_TEST(physical_hold_and_disconnect);RUN_TEST(old_hello_and_invalid_renegotiation);return UNITY_END();}
