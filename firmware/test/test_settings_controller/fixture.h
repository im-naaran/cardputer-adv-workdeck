#pragma once
#include <map>
#include "../test_power_adapter/fixture.h"
#include "../test_wifi_service/fixture.h"
#include "application/settings/settings_page.h"
struct SettingsStore : ConfigFileStore {
  std::map<std::string,std::string> files;
  int reads{0}, writes{0}; bool failWrite{false}, failReload{false};
  ConfigStatus error{ConfigStatus::kOk};
  ConfigStatus read(const std::string& path, std::string& out) override {
    ++reads;
    if (error != ConfigStatus::kOk) return error;
    const auto found = files.find(path);
    if (found == files.end()) return ConfigStatus::kNotFound;
    out = found->second; return ConfigStatus::kOk;
  }
  ConfigStatus replace(const std::string& path, const std::string& value) override {
    ++writes;
    if (failWrite) return ConfigStatus::kWriteFailed;
    files[path] = value;
    if (failReload) error = ConfigStatus::kReadFailed;
    return ConfigStatus::kOk;
  }
};
struct SettingsFixture {
  SettingsStore store; Clock clock; Adapter adapter;
  ScheduledTaskService scheduler; ExecIdGenerator ids;
  CodexController codex{scheduler,ids,[](const auto&,const auto&,uint32_t){return true;},[](const auto&){}};
  CodexConfigService codexConfig{store,codex,[&]{return clock.now;}};
  DisplayConfigService displayConfig{store}; WifiConfigService wifiConfig{store};
  WifiService wifi{wifiConfig,adapter,clock};
  FakePower power; PowerConfigService powerConfig{store,power};
  int brightness{0}, applies{0};
  SettingsController controller{displayConfig,codexConfig,codex,wifiConfig,wifi,powerConfig,[&](const DisplayConfig& value){brightness=value.brightnessLevel*51;++applies;}};
  DisplayAdapter display; SettingsPage page{controller,display}; InputRouter router;
  SettingsFixture() { codex.begin(0); }
  void boot() { controller.loadPower(); controller.loadBrightness(); codexConfig.reload(); controller.refreshCodex(); controller.loadWifi(); }
  void key(Key key) { page.handle(Module::kSettings,router.route({key},Module::kSettings,page.textEditing(Module::kSettings))); }
  void type(const std::string& text) {
    for (char c:text) { KeyEvent event{Key::kCharacter}; event.character=c;event.text=c;
      page.handle(Module::kSettings,router.route(event,Module::kSettings,page.textEditing(Module::kSettings))); }
  }
  void enterWifi() { key(Key::kTab); key(Key::kEnter); }
};
inline void contains(const std::string& text,const char* needle) { TEST_ASSERT_TRUE_MESSAGE(text.find(needle)!=std::string::npos,needle); }
