#include "application/input/input_config_commands.h"
namespace adv {
bool InputConfigCommands::execute(const std::string& line) {
  InputConfigResult result;
  const std::string prefix = "input.config.save ";
  if (line == "input.config.read") result = service_.read();
  else if (line == "input.config.reload") result = service_.reload();
  else if (line.compare(0, prefix.size(), prefix) == 0) result = service_.save(line.substr(prefix.size()));
  else { output_("config error=UnknownCommand"); return false; }
  std::string message = std::string("input.config status=") + configStatusName(result.status);
  if (result.status == ConfigStatus::kOk) message += " file=" + encodeInputConfig(result.config);
  message += " active=" + encodeInputConfig(service_.active());
  if (result.status == ConfigStatus::kReloadFailed) message += " fileMayBeSaved=true; retry input.config.reload";
  output_(message);
  return result.changed;
}
}  // namespace adv
