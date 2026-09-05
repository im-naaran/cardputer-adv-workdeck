#pragma once

#include <functional>
#include "platform/config_file_store.h"

namespace adv {
struct CodexConfig {
  uint32_t refreshIntervalSeconds{300};
  bool valid() const { return refreshIntervalSeconds >= 60 && refreshIntervalSeconds <= 3600; }
};
struct ConfigResult {
  ConfigStatus status{ConfigStatus::kOk};
  CodexConfig config{};
  bool changed{false};
};
ConfigResult parseCodexConfig(const std::string& json);
std::string encodeCodexConfig(const CodexConfig& config);
class CodexController;

class CodexConfigService {
 public:
  CodexConfigService(ConfigFileStore& store, CodexController& controller,
                     std::function<uint32_t()> now)
      : store_(store), controller_(controller), now_(std::move(now)) {}
  ConfigResult read();
  ConfigResult reload();
  ConfigResult save(const std::string& json);
 private:
  ConfigFileStore& store_;
  CodexController& controller_;
  std::function<uint32_t()> now_;
};
}  // namespace adv
