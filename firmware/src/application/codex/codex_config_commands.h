#pragma once

#include "application/codex/codex_controller.h"

namespace adv {
class CodexConfigCommands {
 public:
  using Output = std::function<void(const std::string&)>;
  CodexConfigCommands(CodexConfigService& service, CodexController& controller,
                      Output output, std::function<void()> timeDiagnostic)
      : service_(service), controller_(controller), output_(std::move(output)),
        timeDiagnostic_(std::move(timeDiagnostic)) {}
  // Returns whether a command changed the running task and needs a redraw.
  bool poll(const std::function<int()>& readByte);
 private:
  bool execute();
  CodexConfigService& service_;
  CodexController& controller_;
  Output output_;
  std::function<void()> timeDiagnostic_;
  std::string line_;
  bool overflow_{false};
};
}  // namespace adv
