#include <unity.h>
#include <fstream>
#include <sstream>
#include "application/codex/codex_controller.h"

namespace {
struct Store : adv::ConfigFileStore {
  std::string bytes="{\"refreshIntervalSeconds\":300}";
  adv::ConfigStatus error=adv::ConfigStatus::kOk;
  bool failWrite=false, failAfterWrite=false;
  int reads=0,writes=0;
  adv::ConfigStatus read(const std::string&,std::string& out) override {
    ++reads; out=bytes; return error;
  }
  adv::ConfigStatus replace(const std::string&,const std::string& value) override {
    ++writes;
    if(failWrite)return adv::ConfigStatus::kWriteFailed;
    bytes=value;
    if(failAfterWrite)error=adv::ConfigStatus::kReadFailed;
    return adv::ConfigStatus::kOk;
  }
};
struct Fixture {
  Store store; adv::ScheduledTaskService scheduler; adv::ExecIdGenerator ids;
  uint32_t now=0; int sent=0;
  adv::CodexController controller{scheduler,ids,[&](const auto&,const auto&,uint32_t){++sent;return true;},[](const auto&){}};
  adv::CodexConfigService service{store,controller,[&]{return now;}};
  Fixture(){controller.begin(0);}
};
void parsing() {
  for(auto value:{60u,300u,3600u}) {
    auto result=adv::parseCodexConfig(adv::encodeCodexConfig({value}));
    TEST_ASSERT_TRUE(result.status==adv::ConfigStatus::kOk);
    TEST_ASSERT_EQUAL(value,result.config.refreshIntervalSeconds);
  }
  for(const auto* value:{"59","3601","true","60.5","\"300\"","null","-1"}) {
    TEST_ASSERT_TRUE(adv::parseCodexConfig(std::string("{\"refreshIntervalSeconds\":")+value+"}").status==adv::ConfigStatus::kInvalidConfig);
  }
  for(const auto* value:{"{}","[]","{","{\"refreshIntervalSeconds\":60,\"x\":1}","{\"refreshIntervalSeconds\":60}{}"})
    TEST_ASSERT_TRUE(adv::parseCodexConfig(value).status==adv::ConfigStatus::kInvalidConfig);
  TEST_ASSERT_TRUE(adv::parseCodexConfig(std::string(513,' ')).status==adv::ConfigStatus::kInvalidConfig);
  TEST_ASSERT_TRUE(adv::parseCodexConfig("{\"refreshIntervalSeconds\":60}\r\n ").status==adv::ConfigStatus::kOk);
  std::ifstream sample("data/config/codex.json"); std::stringstream data; data<<sample.rdbuf();
  const auto result=adv::parseCodexConfig(data.str());
  TEST_ASSERT_TRUE(result.status==adv::ConfigStatus::kOk);
  TEST_ASSERT_EQUAL(adv::CodexConfig{}.refreshIntervalSeconds,result.config.refreshIntervalSeconds);
}
void latest_file_and_no_redundant_writes() {
  Fixture f;
  TEST_ASSERT_EQUAL(300,f.service.read().config.refreshIntervalSeconds);
  f.store.bytes=adv::encodeCodexConfig({60});
  TEST_ASSERT_EQUAL(60,f.service.read().config.refreshIntervalSeconds);
  TEST_ASSERT_EQUAL(300000,f.controller.taskState().intervalMs);
  f.now=1000;auto saved=f.service.save(f.store.bytes);
  TEST_ASSERT_TRUE(saved.changed);TEST_ASSERT_EQUAL(0,f.store.writes);
  TEST_ASSERT_EQUAL(60000,f.controller.taskState().intervalMs);
  f.now=50000;TEST_ASSERT_FALSE(f.service.reload().changed);
  TEST_ASSERT_FALSE(f.service.save(f.store.bytes).changed);
  // Inspect the scheduler deadline without a live session or an in-flight network request.
  int fired=0;
  f.scheduler.cancel(adv::ScheduledTaskId::kCodexUsageRefresh);
  f.scheduler.registerTask({adv::ScheduledTaskId::kCodexUsageRefresh,60000,true,false},[&](uint32_t){++fired;},1000);
  f.service.reload();f.scheduler.tick(60999);TEST_ASSERT_EQUAL(0,fired);
  f.scheduler.tick(61000);TEST_ASSERT_EQUAL(1,fired);
}
void failure_and_repair() {
  Fixture f; f.now=100;f.service.save(adv::encodeCodexConfig({60}));
  for(auto error:{adv::ConfigStatus::kNotMounted,adv::ConfigStatus::kReadFailed,adv::ConfigStatus::kNotFound}) {
    f.store.error=error;TEST_ASSERT_TRUE(f.service.reload().status==error);
    TEST_ASSERT_EQUAL(60000,f.controller.taskState().intervalMs);
  }
  f.store.error=adv::ConfigStatus::kOk;f.store.bytes="bad";
  TEST_ASSERT_TRUE(f.service.save(adv::encodeCodexConfig({300})).status==adv::ConfigStatus::kOk);
  const auto before=f.store.bytes;
  TEST_ASSERT_TRUE(f.service.save("{}").status==adv::ConfigStatus::kInvalidConfig);
  TEST_ASSERT_EQUAL_STRING(before.c_str(),f.store.bytes.c_str());
  f.store.failWrite=true;
  TEST_ASSERT_TRUE(f.service.save(adv::encodeCodexConfig({60})).status==adv::ConfigStatus::kWriteFailed);
  TEST_ASSERT_EQUAL_STRING(before.c_str(),f.store.bytes.c_str());
  f.store.failWrite=false;f.store.failAfterWrite=true;
  TEST_ASSERT_TRUE(f.service.save(adv::encodeCodexConfig({60})).status==adv::ConfigStatus::kReloadFailed);
  TEST_ASSERT_EQUAL(300000,f.controller.taskState().intervalMs);
  f.store.error=adv::ConfigStatus::kOk;
  TEST_ASSERT_TRUE(f.service.reload().changed);
  f.scheduler.cancel(adv::ScheduledTaskId::kCodexUsageRefresh);
  TEST_ASSERT_TRUE(f.service.reload().status==adv::ConfigStatus::kApplyFailed);
}
void disabled_and_io_clock() {
  Fixture f;
  f.scheduler.setEnabled(adv::ScheduledTaskId::kCodexUsageRefresh,false,0);
  f.now=0xfffffff0;
  TEST_ASSERT_TRUE(f.service.save(adv::encodeCodexConfig({60})).changed);
  TEST_ASSERT_FALSE(f.controller.taskState().enabled);
  f.scheduler.setEnabled(adv::ScheduledTaskId::kCodexUsageRefresh,true,f.now);
  int fired=0;
  f.scheduler.cancel(adv::ScheduledTaskId::kCodexUsageRefresh);
  f.scheduler.registerTask({adv::ScheduledTaskId::kCodexUsageRefresh,300000,true,false},[&](uint32_t){++fired;},0);
  f.service.reload(); // Apply from injected time after I/O, not from the old loop time zero.
  f.scheduler.tick(f.now+59999);TEST_ASSERT_EQUAL(0,fired);
  f.scheduler.tick(f.now+60000);TEST_ASSERT_EQUAL(1,fired);
}
}
int main(int,char**) {UNITY_BEGIN();RUN_TEST(parsing);RUN_TEST(latest_file_and_no_redundant_writes);RUN_TEST(failure_and_repair);RUN_TEST(disabled_and_io_clock);return UNITY_END();}
