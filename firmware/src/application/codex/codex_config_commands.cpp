#include "application/codex/codex_config_commands.h"

namespace adv {
bool CodexConfigCommands::poll(const std::function<int()>& readByte) {
  bool changed = false;
  // A partial serial line never blocks BLE, input or the scheduler.
  for (size_t count = 0; count < 64; ++count) {
    const int value = readByte();
    if (value < 0) break;
    const char c = static_cast<char>(value);
    if (c == '\r') continue;
    if (c == '\n') {
      if (overflow_) output_("config error=LineTooLong");
      else if (!line_.empty()) changed = execute() || changed;
      line_.clear();
      overflow_ = false;
    } else if (!overflow_) {
      if (line_.empty() && c == 't') {
        timeDiagnostic_();
      } else if (line_.size() == 576) {
        // Discard the entire command, never execute a valid-looking truncated prefix.
        overflow_ = true;
        line_.clear();
      } else {
        line_.push_back(c);
      }
    }
  }
  return changed;
}

bool CodexConfigCommands::execute() {
  ConfigResult result;
  const std::string prefix = "codex.config.save ";
  if (line_ == "codex.config.read") result = service_.read();
  else if (line_ == "codex.config.reload") result = service_.reload();
  else if (line_.compare(0, prefix.size(), prefix) == 0) result = service_.save(line_.substr(prefix.size()));
  else {
    output_("config error=UnknownCommand");
    return false;
  }
  std::string message = std::string("config status=") + configStatusName(result.status);
  if (result.status == ConfigStatus::kOk) message += " file=" + encodeCodexConfig(result.config);
  message += " activeIntervalSeconds=" + std::to_string(controller_.taskState().intervalMs / 1000);
  if (result.status == ConfigStatus::kReloadFailed || result.status == ConfigStatus::kApplyFailed) {
    message += " fileMayBeSaved=true; retry codex.config.reload";
  }
  output_(message);
  return result.changed;
}
}  // namespace adv
