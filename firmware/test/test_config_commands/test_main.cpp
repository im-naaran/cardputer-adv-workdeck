#include <unity.h>
#include <vector>
#include "application/codex/codex_config_commands.h"

void commands_and_scheduler() {
  struct Store : adv::ConfigFileStore {
    std::string bytes=adv::encodeCodexConfig({300});int writes=0;
    adv::ConfigStatus read(const std::string&,std::string& out) override {out=bytes;return adv::ConfigStatus::kOk;}
    adv::ConfigStatus replace(const std::string&,const std::string& value) override {bytes=value;++writes;return adv::ConfigStatus::kOk;}
  } store;
  adv::ScheduledTaskService scheduler;adv::ExecIdGenerator ids;
  uint32_t now=0;int sent=0,diagnostics=0;
  adv::CodexController controller(scheduler,ids,[&](const auto&,const auto&,uint32_t){++sent;return true;},[](const auto&){});
  controller.begin(0); controller.onSessionReady(true,0);
  const auto active=controller.state().inFlightExecId();
  adv::CodexConfigService service(store,controller,[&]{return now;});
  std::vector<std::string> out;
  adv::CodexConfigCommands commands(service,controller,[&](const auto& line){out.push_back(line);},[&]{++diagnostics;});
  std::string input;size_t offset=0;
  auto poll=[&]{return commands.poll([&]()->int {return offset<input.size()?static_cast<unsigned char>(input[offset++]):-1;});};
  input="codex.config.save {\"refreshIntervalSeconds\":60}";
  TEST_ASSERT_FALSE(poll());TEST_ASSERT_EQUAL(300000,controller.taskState().intervalMs);
  now=1000;input+="\r\n";TEST_ASSERT_TRUE(poll());
  TEST_ASSERT_EQUAL(60000,controller.taskState().intervalMs);
  TEST_ASSERT_EQUAL_STRING(active.c_str(),controller.state().inFlightExecId().c_str());
  adv::Message response;response.actionId="codex.usage.read";response.execId=active;response.resultCode="OK";response.windows.push_back({});
  TEST_ASSERT_TRUE(controller.onMessage(response,1001));
  now=50000;input+="codex.config.save {\"refreshIntervalSeconds\":60}\n";TEST_ASSERT_FALSE(poll());
  TEST_ASSERT_EQUAL(1,store.writes);
  scheduler.tick(60999);TEST_ASSERT_EQUAL(1,sent);scheduler.tick(61000);TEST_ASSERT_EQUAL(2,sent);
  input+="t\r\ncodex.config.read\ncodex.config.reload\n";poll();TEST_ASSERT_EQUAL(1,diagnostics);
  input+=std::string(577,'x')+"\n";auto previous=offset;poll();TEST_ASSERT_EQUAL(64,offset-previous);
  while(offset<input.size())poll();
  TEST_ASSERT_EQUAL_STRING("config error=LineTooLong",out.back().c_str());
  input+="unknown\ncodex.config.save {}\ncodex.config.read\n";poll();
  TEST_ASSERT_TRUE(out.back().find("activeIntervalSeconds=60")!=std::string::npos);
  const auto outputs=out.size();poll();TEST_ASSERT_EQUAL(outputs,out.size());
}
int main(int,char**) {UNITY_BEGIN();RUN_TEST(commands_and_scheduler);return UNITY_END();}
