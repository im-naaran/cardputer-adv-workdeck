#include "../test_settings_controller/fixture.h"
namespace {
constexpr auto path="/config/power.json";
void strict_config_boundaries() {
  TEST_ASSERT_EQUAL(160,PowerConfig{}.cpuFrequencyMhz);
  for (auto mhz:{80u,160u,240u}) {
    const auto parsed=parsePowerConfig(encodePowerConfig({mhz}));
    TEST_ASSERT_TRUE(parsed.status==ConfigStatus::kOk);
    TEST_ASSERT_EQUAL(mhz,parsed.config.cpuFrequencyMhz);
  }
  for(auto value:{"0","40","120","256","-1","true","null","160.5","160.0","\"160\"","4294967296"})
    TEST_ASSERT_TRUE(parsePowerConfig(std::string("{\"cpuFrequencyMhz\":")+value+"}").status==ConfigStatus::kInvalidConfig);
  for(auto json:{"{}","[]","{",R"({"cpuFrequencyMhz":160,"extra":1})",R"({"cpuFrequencyMhz":160}x)",
      R"({"cpuFrequencyMhz\u0000extra":160})",R"({"cpuFrequencyMhz":160,"cpuFrequencyMhz\u0000":80})"})
    TEST_ASSERT_TRUE(parsePowerConfig(json).status==ConfigStatus::kInvalidConfig);
  TEST_ASSERT_TRUE(parsePowerConfig(std::string(513,' ')).status==ConfigStatus::kInvalidConfig);
  TEST_ASSERT_TRUE(parsePowerConfig(encodePowerConfig({160})+std::string("\0junk",5)).status==ConfigStatus::kInvalidConfig);
  TEST_ASSERT_TRUE(parsePowerConfig(encodePowerConfig({160})+" \r\n\t").status==ConfigStatus::kOk);
}
void startup_defaults_and_persisted_frequency() {
  SettingsStore store; FakePower power; PowerConfigService service(store,power);
  TEST_ASSERT_TRUE(service.load().status==ConfigStatus::kNotFound);
  TEST_ASSERT_EQUAL(160,power.actual); TEST_ASSERT_EQUAL(0,store.writes);
  for(auto json:{"bad",R"({"cpuFrequencyMhz":120})"}) {
    power.actual=240; store.files[path]=json;
    TEST_ASSERT_TRUE(service.load().status==ConfigStatus::kInvalidConfig);
    TEST_ASSERT_EQUAL(160,power.actual); TEST_ASSERT_EQUAL_STRING(json,store.files[path].c_str());
  }
  for(auto status:{ConfigStatus::kReadFailed,ConfigStatus::kNotMounted}) {
    store.error=status; power.actual=240;
    TEST_ASSERT_TRUE(service.load().status==status);
    TEST_ASSERT_EQUAL(160,power.actual); TEST_ASSERT_EQUAL(0,store.writes);
  }
  store.error=ConfigStatus::kOk;
  for(auto mhz:{80u,160u,240u}) {
    store.files[path]=encodePowerConfig({mhz});
    FakePower restarted; PowerConfigService boot(store,restarted);
    TEST_ASSERT_TRUE(boot.load().status==ConfigStatus::kOk); TEST_ASSERT_EQUAL(mhz,restarted.actual);
  }
  power.actual=240; power.fail=true; store.files[path]=encodePowerConfig({80});
  TEST_ASSERT_TRUE(service.load().status==ConfigStatus::kApplyFailed);
  TEST_ASSERT_EQUAL(240,service.frequencyMhz()); TEST_ASSERT_EQUAL(0,store.writes);
}
void save_errors_retry_and_independent_files() {
  SettingsStore store; FakePower power; PowerConfigService service(store,power);
  for(auto other:{"display","wifi","codex","input"}) store.files[std::string("/config/")+other+".json"]="sentinel";
  auto save=[&](uint32_t mhz){return service.save(encodePowerConfig({mhz}));};
  TEST_ASSERT_TRUE(save(160).status==ConfigStatus::kOk);
  TEST_ASSERT_EQUAL(160,service.frequencyMhz()); TEST_ASSERT_EQUAL(1,store.writes);
  TEST_ASSERT_TRUE(save(160).status==ConfigStatus::kOk); TEST_ASSERT_EQUAL(1,store.writes);
  const auto calls=power.calls.size();
  TEST_ASSERT_TRUE(service.save("{}").status==ConfigStatus::kInvalidConfig);
  TEST_ASSERT_EQUAL(calls,power.calls.size()); TEST_ASSERT_EQUAL(1,store.writes);
  power.fail=true;
  TEST_ASSERT_TRUE(save(80).status==ConfigStatus::kApplyFailed); TEST_ASSERT_EQUAL(1,store.writes);
  TEST_ASSERT_EQUAL(160,power.actual);
  power.fail=false; store.failWrite=true;
  TEST_ASSERT_TRUE(save(80).status==ConfigStatus::kWriteFailed); TEST_ASSERT_EQUAL(80,power.actual);
  TEST_ASSERT_EQUAL(160,parsePowerConfig(store.files[path]).config.cpuFrequencyMhz);
  store.failWrite=false;
  TEST_ASSERT_TRUE(save(80).status==ConfigStatus::kOk);
  store.failReload=true;
  TEST_ASSERT_TRUE(save(240).status==ConfigStatus::kReloadFailed); TEST_ASSERT_EQUAL(240,power.actual);
  TEST_ASSERT_EQUAL(240,parsePowerConfig(store.files[path]).config.cpuFrequencyMhz);
  auto writes=store.writes; store.failReload=false;
  // Read failure may allow an in-memory change but must not overwrite disk.
  TEST_ASSERT_TRUE(save(160).status==ConfigStatus::kReadFailed);
  TEST_ASSERT_EQUAL(160,power.actual); TEST_ASSERT_EQUAL(writes,store.writes);
  store.error=ConfigStatus::kOk;
  TEST_ASSERT_TRUE(save(240).status==ConfigStatus::kOk); TEST_ASSERT_EQUAL(writes,store.writes);
  for(auto other:{"display","wifi","codex","input"})
    TEST_ASSERT_EQUAL_STRING("sentinel",store.files[std::string("/config/")+other+".json"].c_str());
}
struct MismatchedStore : SettingsStore {
  bool mismatch{true};
  ConfigStatus replace(const std::string& p,const std::string& bytes) override {
    return SettingsStore::replace(p,mismatch ? encodePowerConfig({80}) : bytes);
  }
};
void readback_mismatch_and_restore_failure() {
  MismatchedStore store; FakePower power; PowerConfigService service(store,power);
  TEST_ASSERT_TRUE(service.save(encodePowerConfig({160})).status==ConfigStatus::kReloadFailed);
  TEST_ASSERT_EQUAL(160,power.actual);
  store.mismatch=false;
  TEST_ASSERT_TRUE(service.save(encodePowerConfig({160})).status==ConfigStatus::kOk);
  const auto writes=store.writes;
  power.steps={{false,240},{false,240}};
  const auto failed=service.save(encodePowerConfig({80}));
  TEST_ASSERT_TRUE(failed.status==ConfigStatus::kApplyFailed);
  TEST_ASSERT_TRUE(failed.powerStatus==PowerStatus::kRestoreFailed);
  TEST_ASSERT_EQUAL(240,service.frequencyMhz()); TEST_ASSERT_EQUAL(writes,store.writes);
}
}
int main(int,char**) { UNITY_BEGIN(); RUN_TEST(strict_config_boundaries); RUN_TEST(startup_defaults_and_persisted_frequency); RUN_TEST(save_errors_retry_and_independent_files); RUN_TEST(readback_mismatch_and_restore_failure); return UNITY_END(); }
