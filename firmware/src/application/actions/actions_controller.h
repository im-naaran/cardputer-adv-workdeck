#pragma once
#include <array>
#include <functional>
#include "core/exec_id_generator.h"
#include "core/message_codec.h"
namespace adv {
enum class ActionDirectoryStatus { kUnsupported, kNotLoaded, kLoading, kReady, kFailed };
enum class ActionExecutionStatus { kIdle, kRunning, kSucceeded, kFailed, kUnconfirmed };
struct ActionDirectoryState {
  ActionDirectoryStatus directory{ActionDirectoryStatus::kUnsupported};
  std::vector<ActionEntry> entries;
  uint64_t offset{0}, total{0}, nextOffset{0};
  bool hasNextOffset{false};
  size_t selected{0};
  std::string directoryError;
  // One page and one in-flight request per type, never a second execution slot.
  bool hasTotal{false}, selectLast{false};
  std::string directoryId;
  uint64_t targetOffset{0};
  uint32_t started{0};
};
struct ActionExecutionState {
  ActionExecutionStatus status{ActionExecutionStatus::kIdle};
  std::string feedback;
  uint32_t feedbackAt{0};
};
class ActionsController {
 public:
  using Send = std::function<bool(const std::string&, const std::string&, uint32_t)>;
  using Cancel = std::function<void(const std::string&)>;
  ActionsController(ExecIdGenerator& ids, Send send, Cancel cancel)
      : ids_(ids), send_(std::move(send)), cancel_(std::move(cancel)) {}
  void onSessionReady(const std::string& computer, bool list, bool execute, bool shortcut,
                      const std::vector<ActionType>& types);
  void enter(ActionType type, uint32_t now);
  void disconnect();
  void moveSelection(ActionType type, int direction, uint32_t now);
  bool confirm(ActionType type, uint32_t now);
  bool executeById(ActionType type, const std::string& actionId, uint32_t now);
  bool executeByKey(char key, uint32_t now);
  bool onMessage(const Message& message, uint32_t now);
  void tick(uint32_t now);
  const ActionDirectoryState& state(ActionType type) const { return directories_[static_cast<size_t>(type)]; }
  const ActionExecutionState& execution() const { return execution_; }
  const std::string& directoryExecId(ActionType type) const { return state(type).directoryId; }
  const std::string& executionExecId() const { return executionId_; }
 private:
  ActionDirectoryState& directory(ActionType type) { return directories_[static_cast<size_t>(type)]; }
  const ActionDirectoryState& directory(ActionType type) const { return state(type); }
  bool supports(ActionType type) const;
  bool requestPage(ActionType type, uint64_t offset, bool selectLast, uint32_t now);
  bool submitExecution(const std::string& action, const std::string& target, char key, uint32_t now);
  void failDirectory(ActionType type, const std::string& error);
  void feedback(const std::string& text, uint32_t now);
  bool validPage(ActionType type, const Message& message) const;
  ExecIdGenerator& ids_;
  Send send_;
  Cancel cancel_;
  MessageCodec codec_;
  std::array<ActionDirectoryState, 2> directories_{};
  ActionExecutionState execution_;
  std::vector<ActionType> types_;
  bool ready_{false}, listSupported_{false}, executeSupported_{false}, shortcutSupported_{false};
  std::string computer_, executionId_, executionAction_, executionTarget_;
  ActionType executionType_{ActionType::kScript};
  uint32_t executionStarted_{0};
};
}  // namespace adv
