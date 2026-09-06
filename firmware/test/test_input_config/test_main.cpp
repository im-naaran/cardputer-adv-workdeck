#include <unity.h>
#include <map>
#include "application/input/input_config_commands.h"
#include "application/codex/codex_config_commands.h"
using namespace adv;
struct Store : ConfigFileStore {
  std::map<std::string,std::string> files;
  int writes=0;
  ConfigStatus readStatus=ConfigStatus::kOk, writeStatus=ConfigStatus::kOk;
  bool failReread=false;
  ConfigStatus read(const std::string& path,std::string& out) override {
    if(readStatus!=ConfigStatus::kOk)return readStatus;
    if(!files.count(path))return ConfigStatus::kNotFound;
    out=files[path];return ConfigStatus::kOk;
  }
  ConfigStatus replace(const std::string& path,const std::string& bytes) override {
    if(writeStatus!=ConfigStatus::kOk)return writeStatus;
    files[path]=bytes;++writes;
    if(failReread)readStatus=ConfigStatus::kReadFailed;
    return ConfigStatus::kOk;
  }
};
void parsing_and_persistence() {
  for(const auto* json:{"{}","{\"scripts\":{}}","{\"scripts\":{\"directionMapping\":false}}"})
    TEST_ASSERT_EQUAL_INT((int)ConfigStatus::kOk,(int)parseInputConfig(json).status);
  for(const auto* json:{"[]","null","{}{}","{}x","{\"unknown\":{}}","{\"scripts\":null}",
    "{\"scripts\":{\"unknown\":true}}","{\"scripts\":{\"directionMapping\":1}}","{\"scripts\":{\"directionMapping\":null}}",
    "{\"scripts\":{\"directionMapping\":\"false\"}}"})
    TEST_ASSERT_EQUAL_INT((int)ConfigStatus::kInvalidConfig,(int)parseInputConfig(json).status);
  TEST_ASSERT_EQUAL_INT((int)ConfigStatus::kInvalidConfig,(int)parseInputConfig(std::string(513,' ')).status);
  Store store; InputRouter router; InputConfigService service(store,router);
  TEST_ASSERT_EQUAL_INT((int)ConfigStatus::kNotFound,(int)service.reload().status);
  TEST_ASSERT_TRUE(router.config().directionMapping[1]);
  const std::string disabled="{\"scripts\":{\"directionMapping\":false}}";
  TEST_ASSERT_TRUE(service.save(disabled).changed);
  TEST_ASSERT_FALSE(router.config().directionMapping[1]); TEST_ASSERT_TRUE(router.config().directionMapping[0]);
  TEST_ASSERT_FALSE(service.save(disabled).changed); TEST_ASSERT_EQUAL(1,store.writes);
  router.applyConfig({}); TEST_ASSERT_TRUE(service.save(disabled).changed); TEST_ASSERT_EQUAL(1,store.writes);
  InputRouter rebootRouter; InputConfigService reboot(store,rebootRouter);
  reboot.reload(); TEST_ASSERT_FALSE(rebootRouter.config().directionMapping[1]);
  store.writeStatus=ConfigStatus::kWriteFailed;
  TEST_ASSERT_EQUAL_INT((int)ConfigStatus::kWriteFailed,(int)service.save("{}").status);
  TEST_ASSERT_FALSE(router.config().directionMapping[1]);
  store.writeStatus=ConfigStatus::kOk; store.failReread=true;
  TEST_ASSERT_EQUAL_INT((int)ConfigStatus::kReloadFailed,(int)service.save("{}").status);
  TEST_ASSERT_FALSE(router.config().directionMapping[1]);
  store.readStatus=ConfigStatus::kOk; store.failReread=false;
  TEST_ASSERT_TRUE(service.reload().changed); TEST_ASSERT_TRUE(router.config().directionMapping[1]);
  store.files["/config/input.json"]="bad"; TEST_ASSERT_FALSE(service.reload().changed);
  TEST_ASSERT_TRUE(router.config().directionMapping[1]);
  TEST_ASSERT_EQUAL_INT((int)ConfigStatus::kOk,(int)service.save(disabled).status);
  const int writes=store.writes; store.readStatus=ConfigStatus::kNotMounted;
  service.save("{}"); TEST_ASSERT_EQUAL(writes,store.writes); TEST_ASSERT_FALSE(router.config().directionMapping[1]);
}
void shared_serial_dispatch() {
  Store store; InputRouter router; InputConfigService service(store,router);
  ScheduledTaskService scheduler; ExecIdGenerator ids;
  CodexController codex(scheduler,ids,[](const auto&,const auto&,uint32_t){return true;},[](const auto&){});
  codex.begin(0); CodexConfigService codexService(store,codex,[]{return 0;});
  std::vector<std::string> out; int diagnostic=0;
  auto output=[&](const auto& s){out.push_back(s);};
  CodexConfigCommands codexCommands(codexService,codex,output);
  InputConfigCommands inputCommands(service,output);
  ConfigCommandDispatcher dispatcher([&](const auto& line){return codexCommands.execute(line);},
    [&](const auto& line){return inputCommands.execute(line);},output,[&]{++diagnostic;});
  std::string bytes="input.config.save {\"scripts\":{\"directionMapping\":false}}"; size_t offset=0;
  auto poll=[&]{return dispatcher.poll([&]()->int{return offset<bytes.size()?(unsigned char)bytes[offset++]:-1;});};
  TEST_ASSERT_FALSE(poll()); TEST_ASSERT_TRUE(router.config().directionMapping[1]);
  bytes+="\r\n"; TEST_ASSERT_TRUE(poll()); TEST_ASSERT_FALSE(router.config().directionMapping[1]);
  bytes+="t\ncodex.config.save {\"refreshIntervalSeconds\":60}\ninput.config.read\ninput.config.reload\n";
  while(offset<bytes.size())poll();
  TEST_ASSERT_EQUAL(1,diagnostic); TEST_ASSERT_EQUAL(60000,codex.taskState().intervalMs);
  const int writes=store.writes;
  bytes+="input.config.save {}"+std::string(580,' ')+"\n";
  const auto before=offset;poll();TEST_ASSERT_EQUAL(64,offset-before);
  while(offset<bytes.size())poll();
  TEST_ASSERT_EQUAL_STRING("config error=LineTooLong",out.back().c_str());TEST_ASSERT_EQUAL(writes,store.writes);
  bytes+="input.config.save {}\n";TEST_ASSERT_TRUE(poll());TEST_ASSERT_TRUE(router.config().directionMapping[1]);
}
int main(int,char**) {UNITY_BEGIN();RUN_TEST(parsing_and_persistence);RUN_TEST(shared_serial_dispatch);return UNITY_END();}
