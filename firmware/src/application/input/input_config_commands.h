#pragma once
#include "application/input/input_config.h"
#include "core/config_command_dispatcher.h"
namespace adv {
class InputConfigCommands {
 public:
  InputConfigCommands(InputConfigService& service, ConfigCommandDispatcher::Output output)
      : service_(service), output_(std::move(output)) {}
  bool execute(const std::string& line);
 private:
  InputConfigService& service_;
  ConfigCommandDispatcher::Output output_;
};
}  // namespace adv
