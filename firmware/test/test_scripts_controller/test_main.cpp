#include <unity.h>
#include <ArduinoJson.h>
#include <limits>
#include "application/scripts/scripts_controller.h"
#include "core/outgoing_jsonl_queue.h"
#include "core/message_router.h"
#include "core/protocol_constants.h"
using namespace adv;
struct Harness {
  ExecIdGenerator ids;
  OutgoingJsonlQueue queue;
  std::vector<std::string> sent, cancelled;
  bool accept=true;
  uint32_t generation=1;
  ScriptsController controller{ids,[&](const auto& id,const auto& bytes,uint32_t now){
    if(!accept)return false;
    if(!queue.enqueue(id,bytes,now,generation))return false;
    sent.push_back(bytes);return true;
  },[&](const auto& id){cancelled.push_back(id);queue.cancelPending(id);}};
  void drain(uint32_t start=0) {
    for(uint32_t t=start;queue.size() && t-start<10000;t+=10)queue.poll(t,generation,[](const auto&){return true;});
    TEST_ASSERT_EQUAL(0,queue.size());
  }
  void ready(bool list=true,bool execute=true,bool shortcut=true,uint32_t now=0) {
    controller.onSessionReady("pc",list,execute,shortcut,now);
  }
  Message page(uint64_t offset,uint64_t total) {
    JsonDocument doc;
    doc["event"]="response";doc["actionId"]="actions.list";doc["execId"]=controller.directoryExecId();
    doc["result"]["code"]="OK";doc["result"]["msg"]="ok";
    auto data=doc["result"]["data"].to<JsonObject>();
    data["offset"]=offset;data["total"]=total;
    data["nextOffset"]=nullptr;if(total-offset>8)data["nextOffset"]=offset+8;
    auto entries=data["actions"].to<JsonArray>();
    for(uint64_t i=offset;i<total && i-offset<8;++i) {
      auto e=entries.add<JsonObject>();e["type"]="script";
      e["actionId"]="script.item"+std::to_string(i);e["name"]="脚本"+std::to_string(i);
      e["key"]="g";e["effectiveKey"]=nullptr;if(i==0)e["effectiveKey"]="g";
    }
    std::string bytes;serializeJson(doc,bytes);auto decoded=MessageCodec{}.decode(bytes);
    TEST_ASSERT_TRUE_MESSAGE(decoded.ok,decoded.error.c_str()); return decoded.message;
  }
  Message execution(const std::string& outer="actions.shortcut.execute",const std::string& target="script.item100",const std::string& code="OK") {
    Message m;m.actionId=outer;m.execId=controller.executionExecId();m.resultCode=code;
    m.executedActionId=target;m.executedName="末项";m.hasExitCode=true;m.exitCode=0;return m;
  }
};
void all_pages_and_boundaries() {
  Harness h;h.ready();h.drain();auto& c=h.controller;
  TEST_ASSERT_TRUE(c.onMessage(h.page(0,101),1));
  c.moveSelection(-1,2);TEST_ASSERT_EQUAL(1,h.sent.size());
  for(uint64_t i=0;i<101;++i) {
    TEST_ASSERT_EQUAL_UINT64(i,c.state().offset+c.state().selected);
    TEST_ASSERT_LESS_OR_EQUAL(8,c.state().entries.size());
    if(i==100)break;
    c.moveSelection(1,3);
    if(!c.directoryExecId().empty()) {
      TEST_ASSERT_FALSE(c.confirm(4));
      c.moveSelection(1,4); // Loading input is not accumulated.
      const auto next=(i/8+1)*8;h.drain(2000);
      TEST_ASSERT_TRUE(c.onMessage(h.page(next,101),5));
    }
  }
  const auto count=h.sent.size();c.moveSelection(1,6);TEST_ASSERT_EQUAL(count,h.sent.size());
  TEST_ASSERT_TRUE(c.confirm(7));
  TEST_ASSERT_TRUE(h.sent.back().find("script.item100")!=std::string::npos);
  h.drain(4000);TEST_ASSERT_TRUE(c.onMessage(h.execution("scripts.execute"),8));
  for(int i=100;i>0;--i) {
    c.moveSelection(-1,9);
    if(!c.directoryExecId().empty()) {
      h.drain(6000);TEST_ASSERT_TRUE(c.onMessage(h.page(((i-1)/8)*8,101),10));
    }
    TEST_ASSERT_EQUAL_UINT64(i-1,c.state().offset+c.state().selected);
  }
  TEST_ASSERT_EQUAL(1,c.state().entries[0].effectiveKey.size());
  TEST_ASSERT_TRUE(c.state().entries[1].effectiveKey.empty());
  c.moveSelection(1,11);TEST_ASSERT_TRUE(c.confirm(12));
  TEST_ASSERT_TRUE(h.sent.back().find("script.item1")!=std::string::npos);
}
void failed_page_retry_and_atomic_cache() {
  Harness h;h.ready();h.drain();auto& c=h.controller;
  c.onMessage(h.page(0,17),1);
  for(int i=0;i<8;++i)c.moveSelection(1,2);
  h.drain(2000);auto invalid=h.page(8,18);
  TEST_ASSERT_TRUE(c.onMessage(invalid,3));TEST_ASSERT_EQUAL_UINT64(0,c.state().offset);
  TEST_ASSERT_EQUAL_INT((int)ScriptsDirectoryStatus::kFailed,(int)c.state().directory);
  const auto previous=h.sent.size();c.moveSelection(1,4);TEST_ASSERT_EQUAL(previous,h.sent.size());
  TEST_ASSERT_TRUE(c.confirm(5));TEST_ASSERT_TRUE(h.sent.back().find("actions.list")!=std::string::npos);
  h.drain(4000);auto good=h.page(8,17);c.onMessage(good,6);
  TEST_ASSERT_EQUAL_UINT64(8,c.state().offset);TEST_ASSERT_EQUAL(0,c.state().selected);
  TEST_ASSERT_TRUE(c.executionExecId().empty());
  TEST_ASSERT_TRUE(c.confirm(7));TEST_ASSERT_TRUE(h.sent.back().find("script.item8")!=std::string::npos);
}
void malformed_page_metadata() {
  for(int kind=0;kind<5;++kind) {
    Harness h;h.ready();h.drain();auto& c=h.controller;auto m=h.page(0,17);
    if(kind==0)m.offset=8;
    if(kind==1)m.scripts.push_back({});
    if(kind==2)m.nextOffset=16;
    if(kind==3)m.hasNextOffset=false;
    if(kind==4)m.total=0;
    TEST_ASSERT_TRUE(c.onMessage(m,1));TEST_ASSERT_TRUE(c.state().entries.empty());
    TEST_ASSERT_EQUAL_INT((int)ScriptsDirectoryStatus::kFailed,(int)c.state().directory);
  }
}
void shortcuts_independent_and_busy() {
  Harness h;h.ready(false);auto& c=h.controller;
  TEST_ASSERT_TRUE(c.executeByKey('G',0));TEST_ASSERT_TRUE(h.sent.back().find("\"key\":\"g\"")!=std::string::npos);
  TEST_ASSERT_FALSE(c.executeByKey('h',1));TEST_ASSERT_EQUAL(1,h.sent.size());
  TEST_ASSERT_EQUAL_INT((int)ScriptExecutionStatus::kRunning,(int)c.state().execution);
  auto response=h.execution();h.drain();TEST_ASSERT_TRUE(c.onMessage(response,2));
  TEST_ASSERT_EQUAL_INT((int)ScriptExecutionStatus::kSucceeded,(int)c.state().execution);
  TEST_ASSERT_TRUE(c.state().feedback.find("末项")!=std::string::npos);
  TEST_ASSERT_FALSE(c.executeByKey('1',3));
  Harness list;list.ready();list.drain();list.controller.onMessage(list.page(0,1),1);
  TEST_ASSERT_TRUE(list.controller.confirm(2));TEST_ASSERT_FALSE(list.controller.executeByKey('g',3));
  TEST_ASSERT_EQUAL(2,list.sent.size());
}
void capabilities_empty_and_duplicate_hello() {
  Harness h;h.ready();const auto id=h.controller.directoryExecId();h.ready();
  TEST_ASSERT_EQUAL(1,h.sent.size());TEST_ASSERT_EQUAL_STRING(id.c_str(),h.controller.directoryExecId().c_str());
  h.drain();h.controller.onMessage(h.page(0,0),1);TEST_ASSERT_FALSE(h.controller.confirm(2));
  h.controller.moveSelection(1,2);h.controller.moveSelection(-1,2);TEST_ASSERT_EQUAL(1,h.sent.size());
  Harness old;old.ready(false,false,false);TEST_ASSERT_TRUE(old.sent.empty());
  TEST_ASSERT_FALSE(old.controller.executeByKey('g',3));TEST_ASSERT_TRUE(old.sent.empty());
  Harness separate;separate.ready(true,false,true);separate.drain();
  separate.controller.onMessage(separate.page(0,1),1);TEST_ASSERT_FALSE(separate.controller.confirm(2));
  TEST_ASSERT_TRUE(separate.controller.executeByKey('g',3));
  Harness noShortcut;noShortcut.ready(true,true,false);noShortcut.drain();
  noShortcut.controller.onMessage(noShortcut.page(0,1),1);TEST_ASSERT_FALSE(noShortcut.controller.executeByKey('g',2));
  TEST_ASSERT_TRUE(noShortcut.controller.confirm(3));
}
void timeouts_and_queue_rejection() {
  Harness h;h.ready();auto& c=h.controller;
  TEST_ASSERT_TRUE(c.executeByKey('g',0));auto late=h.execution();
  c.tick(14999);TEST_ASSERT_FALSE(c.directoryExecId().empty());
  c.tick(15000);TEST_ASSERT_TRUE(c.directoryExecId().empty());
  TEST_ASSERT_EQUAL(1,h.queue.size());
  c.tick(44999);TEST_ASSERT_FALSE(c.executionExecId().empty());
  c.tick(45000);TEST_ASSERT_TRUE(c.executionExecId().empty());TEST_ASSERT_EQUAL(0,h.queue.size());
  TEST_ASSERT_EQUAL_INT((int)ScriptExecutionStatus::kUnconfirmed,(int)c.state().execution);
  c.tick(90000);TEST_ASSERT_EQUAL(2,h.sent.size());TEST_ASSERT_FALSE(c.onMessage(late,90001));
  TEST_ASSERT_TRUE(c.executeByKey('h',90002));TEST_ASSERT_FALSE(c.onMessage(late,90003));
  h.accept=false; // Explicit retry of directory only; execution never retries automatically.
  TEST_ASSERT_FALSE(c.confirm(90004));
  Harness reject;reject.accept=false;reject.ready();
  TEST_ASSERT_TRUE(reject.controller.directoryExecId().empty());
  TEST_ASSERT_FALSE(reject.controller.executeByKey('g',0));
  TEST_ASSERT_EQUAL_INT((int)ScriptExecutionStatus::kFailed,(int)reject.controller.state().execution);
  Harness wrap;const uint32_t start=UINT32_MAX-100;wrap.ready(false,true,true,start);
  wrap.controller.executeByKey('g',start);wrap.controller.tick(start+45000u);
  TEST_ASSERT_TRUE(wrap.controller.executionExecId().empty());
  Harness receiveFirst;receiveFirst.ready(false);receiveFirst.controller.executeByKey('g',0);
  const auto expired=receiveFirst.execution();
  TEST_ASSERT_FALSE(receiveFirst.controller.onMessage(expired,45000));
  TEST_ASSERT_EQUAL_INT((int)ScriptExecutionStatus::kUnconfirmed,(int)receiveFirst.controller.state().execution);
  Harness latePage;latePage.ready();const auto expiredPage=latePage.page(0,1);
  TEST_ASSERT_FALSE(latePage.controller.onMessage(expiredPage,15000));
  TEST_ASSERT_TRUE(latePage.controller.state().entries.empty());
}
void response_correlations_and_session_reset() {
  Harness h;h.ready();h.drain();auto& c=h.controller;auto page=h.page(0,1);
  auto wrong=page;wrong.event=MessageEvent::kRequest;TEST_ASSERT_FALSE(c.onMessage(wrong,1));
  wrong=page;wrong.execId="other";TEST_ASSERT_FALSE(c.onMessage(wrong,1));
  c.onMessage(page,1);c.confirm(2);h.drain(2000);auto response=h.execution("scripts.execute","script.item0");
  wrong=response;wrong.executedActionId="script.other";TEST_ASSERT_FALSE(c.onMessage(wrong,3));
  wrong=response;wrong.actionId="actions.shortcut.execute";TEST_ASSERT_FALSE(c.onMessage(wrong,3));
  wrong=response;wrong.event=MessageEvent::kRequest;TEST_ASSERT_FALSE(c.onMessage(wrong,3));
  c.onSessionReady("new-pc",true,true,true,4);
  TEST_ASSERT_TRUE(c.state().entries.empty());TEST_ASSERT_TRUE(c.executionExecId().empty());
  TEST_ASSERT_FALSE(c.onMessage(page,5));TEST_ASSERT_FALSE(c.onMessage(response,5));
  c.executeByKey('g',6);auto old=c.executionExecId();c.disconnect();
  TEST_ASSERT_TRUE(c.state().feedback.empty());TEST_ASSERT_EQUAL(0,h.queue.size());
  h.ready();TEST_ASSERT_FALSE(c.onMessage(page,7));
  c.executeByKey('g',8);TEST_ASSERT_TRUE(old!=c.executionExecId());
}
void response_errors_and_router() {
  for(const auto* code:{"ERROR","BUSY","TIMEOUT"}) {
    Harness h;h.ready(false);auto& c=h.controller;c.executeByKey('g',0);h.drain();
    auto response=h.execution("actions.shortcut.execute","",code);
    response.resultMessage="未绑定快捷键";
    MessageRouter router;router.registerHandler(protocol::kShortcutExecuteAction,[&](const auto& m){c.onMessage(m,1);});
    TEST_ASSERT_TRUE(router.route(response));TEST_ASSERT_TRUE(c.executionExecId().empty());
    const auto expected=std::string(code)=="TIMEOUT"?ScriptExecutionStatus::kUnconfirmed:ScriptExecutionStatus::kFailed;
    TEST_ASSERT_EQUAL_INT((int)expected,(int)c.state().execution);
  }
}
int main(int,char**) {
  UNITY_BEGIN();RUN_TEST(all_pages_and_boundaries);RUN_TEST(failed_page_retry_and_atomic_cache);
  RUN_TEST(malformed_page_metadata);RUN_TEST(shortcuts_independent_and_busy);
  RUN_TEST(capabilities_empty_and_duplicate_hello);RUN_TEST(timeouts_and_queue_rejection);
  RUN_TEST(response_correlations_and_session_reset);RUN_TEST(response_errors_and_router);
  return UNITY_END();
}
