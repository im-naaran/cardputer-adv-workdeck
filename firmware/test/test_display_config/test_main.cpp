#include <unity.h>
#include <map>
#include "application/settings/display_config.h"
#include "application/wifi/wifi_config.h"
using namespace adv;
namespace {
struct Store : ConfigFileStore {
  std::map<std::string,std::string> files;
  ConfigStatus error{ConfigStatus::kOk};
  bool failWrite{false}, failReload{false}, corruptReload{false};
  int writes{0};
  ConfigStatus read(const std::string& path, std::string& out) override {
    if (error != ConfigStatus::kOk) return error;
    if (!files.count(path)) return ConfigStatus::kNotFound;
    out = files[path]; return ConfigStatus::kOk;
  }
  ConfigStatus replace(const std::string& path,const std::string& bytes) override {
    ++writes;
    if (failWrite) return ConfigStatus::kWriteFailed;
    files[path] = corruptReload ? "{}" : bytes;
    if (failReload) error = ConfigStatus::kReadFailed;
    return ConfigStatus::kOk;
  }
};
void persistence() {
  Store store; DisplayConfigService service(store);
  TEST_ASSERT_TRUE(service.reload().status == ConfigStatus::kNotFound);
  TEST_ASSERT_TRUE(service.configurationStatus() == ConfigStatus::kNotFound);
  const auto first = encodeDisplayConfig(DisplayConfig{3});
  const auto next = encodeDisplayConfig(DisplayConfig{5});
  store.files["/config/codex.json"] = "sentinel";
  TEST_ASSERT_TRUE(service.save(first).status == ConfigStatus::kOk);
  TEST_ASSERT_TRUE(service.saved() == DisplayConfig{3});
  TEST_ASSERT_FALSE(service.save(first).changed);
  TEST_ASSERT_EQUAL(1,store.writes);
  TEST_ASSERT_TRUE(service.save("{}").status == ConfigStatus::kInvalidConfig);
  store.failWrite=true;
  TEST_ASSERT_TRUE(service.save(next).status == ConfigStatus::kWriteFailed);
  TEST_ASSERT_TRUE(service.saved() == DisplayConfig{3});
  TEST_ASSERT_EQUAL_STRING(first.c_str(),store.files["/config/display.json"].c_str());
  store.failWrite=false; store.failReload=true;
  TEST_ASSERT_TRUE(service.save(next).status == ConfigStatus::kReloadFailed);
  TEST_ASSERT_TRUE(service.saved() == DisplayConfig{3});
  TEST_ASSERT_TRUE(service.configurationStatus() == ConfigStatus::kReadFailed);
  store.failReload=false; store.error=ConfigStatus::kOk;
  int writes=store.writes;
  TEST_ASSERT_TRUE(service.save(next).status == ConfigStatus::kOk);
  TEST_ASSERT_EQUAL(writes,store.writes); // Recovery rereads without rewriting the same value.
  TEST_ASSERT_TRUE(service.saved() == DisplayConfig{5});
  for (auto error : {ConfigStatus::kNotMounted,ConfigStatus::kReadFailed}) {
    store.error=error;
    TEST_ASSERT_TRUE(service.reload().status==error);
    TEST_ASSERT_TRUE(service.saved()==DisplayConfig{5});
    TEST_ASSERT_TRUE(service.save(first).status==error);
    TEST_ASSERT_EQUAL(writes,store.writes);
  }
  store.error=ConfigStatus::kOk; store.files["/config/display.json"]="bad";
  TEST_ASSERT_TRUE(service.reload().status==ConfigStatus::kInvalidConfig);
  TEST_ASSERT_TRUE(service.saved()==DisplayConfig{5});
  TEST_ASSERT_TRUE(service.save(first).status==ConfigStatus::kOk);
  DisplayConfigService restarted(store);
  TEST_ASSERT_TRUE(restarted.reload().status==ConfigStatus::kOk);
  TEST_ASSERT_TRUE(restarted.saved()==DisplayConfig{3});
  TEST_ASSERT_EQUAL_STRING("sentinel",store.files["/config/codex.json"].c_str());
  store.corruptReload=true;
  TEST_ASSERT_TRUE(service.save(next).status==ConfigStatus::kReloadFailed);
  TEST_ASSERT_TRUE(service.saved()==DisplayConfig{3});
}
void validation() {
  TEST_ASSERT_EQUAL(3,DisplayConfig{}.brightnessLevel);
  for (uint8_t n=1;n<=5;++n) TEST_ASSERT_TRUE(parseDisplayConfig(encodeDisplayConfig({n})).status==ConfigStatus::kOk);
  for (auto n:{"0","6","256","-1","true","null","3.5","\"3\""})
    TEST_ASSERT_TRUE(parseDisplayConfig(std::string("{\"brightnessLevel\":")+n+"}").status==ConfigStatus::kInvalidConfig);
  for (auto json:{"{}","[]","{","{\"brightnessLevel\":3,\"x\":1}","{\"brightnessLevel\":3}x"})
    TEST_ASSERT_TRUE(parseDisplayConfig(json).status==ConfigStatus::kInvalidConfig);
  TEST_ASSERT_TRUE(parseDisplayConfig(std::string(513,' ')).status==ConfigStatus::kInvalidConfig);
  TEST_ASSERT_TRUE(parseDisplayConfig("{\"brightnessLevel\":3} \r\n").status==ConfigStatus::kOk);
}
}
int main(int,char**) { UNITY_BEGIN(); RUN_TEST(validation); RUN_TEST(persistence); return UNITY_END(); }
