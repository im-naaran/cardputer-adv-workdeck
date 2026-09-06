#include "core/config_command_dispatcher.h"

namespace adv {
bool ConfigCommandDispatcher::poll(const std::function<int()>& readByte) {
  bool changed = false;
  // A partial serial line never blocks BLE, input or the scheduler.
  for (size_t count = 0; count < 64; ++count) {
    const int value = readByte();
    if (value < 0) break;
    const char c = static_cast<char>(value);
    if (c == '\r') continue;
    if (c == '\n') {
      if (overflow_) output_("config error=LineTooLong");
      else if (!line_.empty()) {
        if (line_.compare(0, 13, "codex.config.") == 0 && codex_) changed = codex_(line_) || changed;
        else if (line_.compare(0, 13, "input.config.") == 0 && input_) changed = input_(line_) || changed;
        else output_("config error=UnknownCommand");
      }
      line_.clear();
      overflow_ = false;
    } else if (!overflow_) {
      if (line_.empty() && c == 't') {
        diagnostic_();
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

}  // namespace adv
