#pragma once

#include "application/codex/codex_controller.h"
#include <functional>

namespace adv {
class CodexConfigCommands {
 public:
  using Output = std::function<void(const std::string&)>;
  CodexConfigCommands(CodexConfigService& service, CodexController& controller,
                      Output output)
      : service_(service), controller_(controller), output_(std::move(output)) {}
  // Returns whether a command changed the running task and needs a redraw.
  bool execute(const std::string& line);
 private:
  CodexConfigService& service_;
  CodexController& controller_;
  Output output_;
};
}  // namespace adv
