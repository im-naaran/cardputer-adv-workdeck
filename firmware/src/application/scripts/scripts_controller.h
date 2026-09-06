#pragma once
#include <functional>
#include "core/exec_id_generator.h"
#include "core/message_codec.h"

namespace adv {
enum class ScriptsDirectoryStatus { kUnsupported, kLoading, kReady, kFailed };
enum class ScriptExecutionStatus { kIdle, kRunning, kSucceeded, kFailed, kUnconfirmed };
struct ScriptsState {
  ScriptsDirectoryStatus directory{ScriptsDirectoryStatus::kUnsupported};
  ScriptExecutionStatus execution{ScriptExecutionStatus::kIdle};
  std::vector<ScriptEntry> entries;
  uint64_t offset{0}, total{0}, nextOffset{0};
  bool hasNextOffset{false};
  size_t selected{0};
  std::string directoryError;
  std::string feedback;
  uint32_t feedbackAt{0};
};
class ScriptsController {
 public:
  using Send = std::function<bool(const std::string&, const std::string&, uint32_t)>;
  using Cancel = std::function<void(const std::string&)>;
  ScriptsController(ExecIdGenerator& ids, Send send, Cancel cancel)
      : ids_(ids), send_(std::move(send)), cancel_(std::move(cancel)) {}
  void onSessionReady(const std::string& computer, bool list, bool execute, bool shortcut, uint32_t now);
  void disconnect();
  void moveSelection(int direction, uint32_t now);
  bool confirm(uint32_t now);
  bool executeById(const std::string& actionId, uint32_t now);
  bool executeByKey(char key, uint32_t now);
  bool onMessage(const Message& message, uint32_t now);
  void tick(uint32_t now);
  const ScriptsState& state() const { return state_; }
  const std::string& directoryExecId() const { return directoryId_; }
  const std::string& executionExecId() const { return executionId_; }
 private:
  bool requestPage(uint64_t offset, bool selectLast, uint32_t now);
  bool submitExecution(const std::string& action, const std::string& target, char key, uint32_t now);
  void failDirectory(const std::string& error);
  void feedback(const std::string& text, uint32_t now);
  bool validPage(const Message& message) const;
  ExecIdGenerator& ids_;
  Send send_;
  Cancel cancel_;
  MessageCodec codec_;
  ScriptsState state_;
  bool ready_{false}, listSupported_{false}, executeSupported_{false}, shortcutSupported_{false};
  bool hasTotal_{false}, selectLast_{false};
  std::string computer_, directoryId_, executionId_, executionAction_, executionTarget_;
  uint64_t targetOffset_{0};
  uint32_t directoryStarted_{0}, executionStarted_{0};
};
}  // namespace adv
