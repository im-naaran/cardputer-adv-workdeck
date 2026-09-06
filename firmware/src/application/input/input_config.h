#pragma once
#include "core/input_router.h"
#include "platform/config_file_store.h"

namespace adv {
struct InputConfigResult {
  ConfigStatus status{ConfigStatus::kOk};
  InputConfig config{};
  bool changed{false};
};
InputConfigResult parseInputConfig(const std::string& json);
std::string encodeInputConfig(const InputConfig& config);
class InputConfigService {
 public:
  InputConfigService(ConfigFileStore& store, InputRouter& router) : store_(store), router_(router) {}
  InputConfigResult read();
  InputConfigResult reload();
  InputConfigResult save(const std::string& json);
  const InputConfig& active() const { return router_.config(); }
 private:
  ConfigFileStore& store_;
  InputRouter& router_;
};
}  // namespace adv
