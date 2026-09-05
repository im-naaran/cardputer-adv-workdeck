#include <unity.h>
#include <fstream>
#include <filesystem>
#include <unistd.h>
#include "platform/config_file_store.h"

namespace {
struct Store : adv::PlatformConfigFileStore {
  using PlatformConfigFileStore::PlatformConfigFileStore;
  bool writeFail=false, corrupt=false, renameFail=false;
  bool writeTemporary(const std::string& path,const std::string& value) override {
    if(writeFail){PlatformConfigFileStore::writeTemporary(path,value.substr(0,1));return false;}
    return PlatformConfigFileStore::writeTemporary(path,corrupt?"corrupt":value);
  }
  bool renameFile(const std::string& from,const std::string& to) override {
    return !renameFail && PlatformConfigFileStore::renameFile(from,to);
  }
};
void file_operations_and_failure_preservation() {
  char pattern[]="/tmp/adv-config-XXXXXX";const auto* dir=mkdtemp(pattern);TEST_ASSERT_NOT_NULL(dir);
  Store store(dir);std::string out="unchanged";
  TEST_ASSERT_TRUE(store.read("/config/codex.json",out)==adv::ConfigStatus::kNotMounted);
  TEST_ASSERT_TRUE(store.begin());
  TEST_ASSERT_TRUE(store.read("/config/codex.json",out)==adv::ConfigStatus::kNotFound);
  TEST_ASSERT_TRUE(store.replace("/config/codex.json","old")==adv::ConfigStatus::kOk);
  TEST_ASSERT_TRUE(store.replace("/config/codex.json","new")==adv::ConfigStatus::kOk);
  for(int mode=0;mode<3;++mode) {
    store.writeFail=mode==0;store.corrupt=mode==1;store.renameFail=mode==2;
    TEST_ASSERT_TRUE(store.replace("/config/codex.json","replacement")==adv::ConfigStatus::kWriteFailed);
    TEST_ASSERT_TRUE(store.read("/config/codex.json",out)==adv::ConfigStatus::kOk);
    TEST_ASSERT_EQUAL_STRING("new",out.c_str());
  }
  TEST_ASSERT_TRUE(store.read("/config",out)==adv::ConfigStatus::kReadFailed);
  std::ofstream(std::string(dir)+"/big")<<std::string(513,'x');
  TEST_ASSERT_TRUE(store.read("/big",out)==adv::ConfigStatus::kInvalidConfig);
  TEST_ASSERT_EQUAL_STRING("new",out.c_str());
  std::filesystem::remove_all(dir);
}
}
int main(int,char**) {UNITY_BEGIN();RUN_TEST(file_operations_and_failure_preservation);return UNITY_END();}
