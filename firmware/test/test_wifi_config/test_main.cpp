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
  Store store; WifiConfigService service(store);
  TEST_ASSERT_TRUE(service.reload().status == ConfigStatus::kNotFound);
  TEST_ASSERT_TRUE(service.configurationStatus() == ConfigStatus::kNotFound);
  const auto first = encodeWifiConfig((WifiConfig{"net", "account", "a"}));
  const auto next = encodeWifiConfig((WifiConfig{"other", "", "12345678"}));
  store.files["/config/codex.json"] = "sentinel";
  TEST_ASSERT_TRUE(service.save(first).status == ConfigStatus::kOk);
  TEST_ASSERT_TRUE(service.saved() == (WifiConfig{"net", "account", "a"}));
  TEST_ASSERT_FALSE(service.save(first).changed);
  TEST_ASSERT_EQUAL(1,store.writes);
  TEST_ASSERT_TRUE(service.save("{}").status == ConfigStatus::kInvalidConfig);
  store.failWrite=true;
  TEST_ASSERT_TRUE(service.save(next).status == ConfigStatus::kWriteFailed);
  TEST_ASSERT_TRUE(service.saved() == (WifiConfig{"net", "account", "a"}));
  TEST_ASSERT_EQUAL_STRING(first.c_str(),store.files["/config/wifi.json"].c_str());
  store.failWrite=false; store.failReload=true;
  TEST_ASSERT_TRUE(service.save(next).status == ConfigStatus::kReloadFailed);
  TEST_ASSERT_TRUE(service.saved() == (WifiConfig{"net", "account", "a"}));
  TEST_ASSERT_TRUE(service.configurationStatus() == ConfigStatus::kReadFailed);
  store.failReload=false; store.error=ConfigStatus::kOk;
  int writes=store.writes;
  TEST_ASSERT_TRUE(service.save(next).status == ConfigStatus::kOk);
  TEST_ASSERT_EQUAL(writes,store.writes); // Recovery rereads without rewriting the same value.
  TEST_ASSERT_TRUE(service.saved() == (WifiConfig{"other", "", "12345678"}));
  for (auto error : {ConfigStatus::kNotMounted,ConfigStatus::kReadFailed}) {
    store.error=error;
    TEST_ASSERT_TRUE(service.reload().status==error);
    TEST_ASSERT_TRUE(service.saved()==(WifiConfig{"other", "", "12345678"}));
    TEST_ASSERT_TRUE(service.save(first).status==error);
    TEST_ASSERT_EQUAL(writes,store.writes);
  }
  store.error=ConfigStatus::kOk; store.files["/config/wifi.json"]="bad";
  TEST_ASSERT_TRUE(service.reload().status==ConfigStatus::kInvalidConfig);
  TEST_ASSERT_TRUE(service.saved()==(WifiConfig{"other", "", "12345678"}));
  TEST_ASSERT_TRUE(service.save(first).status==ConfigStatus::kOk);
  WifiConfigService restarted(store);
  TEST_ASSERT_TRUE(restarted.reload().status==ConfigStatus::kOk);
  TEST_ASSERT_TRUE(restarted.saved()==(WifiConfig{"net", "account", "a"}));
  TEST_ASSERT_EQUAL_STRING("sentinel",store.files["/config/codex.json"].c_str());
  store.corruptReload=true;
  TEST_ASSERT_TRUE(service.save(next).status==ConfigStatus::kReloadFailed);
  TEST_ASSERT_TRUE(service.saved()==(WifiConfig{"net", "account", "a"}));
}
void validation() {
  auto valid=[](const WifiConfig& c) { return parseWifiConfig(encodeWifiConfig(c)).status==ConfigStatus::kOk; };
  TEST_ASSERT_TRUE(valid({"中文网络", "", ""}));
  TEST_ASSERT_TRUE(valid({" net ", " user ", " x "}));
  TEST_ASSERT_TRUE(valid({"net", "a", "x"}));
  TEST_ASSERT_FALSE(valid({"net", "", "x"}));
  TEST_ASSERT_FALSE(valid({"net", "a", ""}));
  TEST_ASSERT_TRUE(valid({std::string(32,'s'),std::string(64,'u'),std::string(64,'p')}));
  TEST_ASSERT_FALSE(valid({std::string(33,'s'),"u","p"}));
  TEST_ASSERT_FALSE(valid({"s",std::string(65,'u'),"p"}));
  TEST_ASSERT_FALSE(valid({"s","u",std::string(65,'p')}));
  TEST_ASSERT_FALSE(valid({"","u","p"}));
  for (int length:{8,63}) TEST_ASSERT_TRUE(valid({"net","",std::string(length,'p')}));
  TEST_ASSERT_TRUE(valid({"net","",std::string(64,'a')}));
  TEST_ASSERT_FALSE(valid({"net","",std::string(64,'g')}));
  TEST_ASSERT_FALSE(valid({"net","",std::string(7,'p')}));
  const WifiConfig escaped{std::string(32,'\"'),std::string(64,'\\'),std::string(64,'\"')};
  auto encoded=encodeWifiConfig(escaped);
  TEST_ASSERT_TRUE(encoded.size()<=kMaxConfigBytes);
  TEST_ASSERT_TRUE(parseWifiConfig(encoded).config==escaped);
  for (auto invalid : {std::string("x\0y",3),std::string("\n"),std::string("\x7f"),std::string("\xc0\xaf"),
      std::string("\xed\xa0\x80"),std::string("\xf4\x90\x80\x80"),std::string("\xe4\xb8"),std::string("\xc2\x85"),std::string("\x80")}) {
    TEST_ASSERT_FALSE(valid({invalid,"u","p"}));
    TEST_ASSERT_FALSE(valid({"s",invalid,"p"}));
    TEST_ASSERT_FALSE(valid({"s","u",invalid}));
  }
  for (auto json:{"{}","[]","{","{\"ssid\":\"n\",\"username\":null,\"password\":\"\"}",
      "{\"ssid\":\"n\",\"username\":\"\",\"password\":\"\",\"type\":\"personal\"}",
      "{\"ssid\":\"n\",\"username\":\"\",\"password\":\"\"}{}",
      "{\"ssid\":\"n\\u0000x\",\"username\":\"\",\"password\":\"\"}"})
    TEST_ASSERT_TRUE(parseWifiConfig(json).status==ConfigStatus::kInvalidConfig);
  TEST_ASSERT_TRUE(parseWifiConfig(std::string(513,' ')).status==ConfigStatus::kInvalidConfig);
}
}
int main(int,char**) { UNITY_BEGIN(); RUN_TEST(validation); RUN_TEST(persistence); return UNITY_END(); }
