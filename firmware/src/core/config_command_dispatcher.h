#pragma once
#include <functional>
#include <string>
namespace adv {
class ConfigCommandDispatcher {
 public:
  using Handler = std::function<bool(const std::string&)>;
  using Output = std::function<void(const std::string&)>;
  ConfigCommandDispatcher(Handler codex, Handler input, Output output, std::function<void()> diagnostic)
      : codex_(std::move(codex)), input_(std::move(input)), output_(std::move(output)), diagnostic_(std::move(diagnostic)) {}
  bool poll(const std::function<int()>& readByte);
 private:
  Handler codex_, input_;
  Output output_;
  std::function<void()> diagnostic_;
  std::string line_;
  bool overflow_{false};
};
}  // namespace adv
