#include "application/codex/codex_config_commands.h"

namespace adv {
bool CodexConfigCommands::execute(const std::string& line) {
  ConfigResult result;
  const std::string prefix = "codex.config.save ";
  if (line == "codex.config.read") result = service_.read();
  else if (line == "codex.config.reload") result = service_.reload();
  else if (line.compare(0, prefix.size(), prefix) == 0) result = service_.save(line.substr(prefix.size()));
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
