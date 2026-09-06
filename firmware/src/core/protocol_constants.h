#pragma once

#include <cstddef>
#include <cstdint>

namespace adv::protocol {

constexpr int kVersion = 1;
constexpr size_t kMaxJsonBytes = 4096;
constexpr size_t kSafeChunkBytes = 20;
constexpr uint32_t kNotifyChunkDelayMs = 10;
constexpr const char* kServiceUuid = "5fd5b6a4-60a1-48e1-a4f3-69c9cab741d2";
constexpr const char* kAdvToPcNotifyUuid = "a6c791a8-824d-4f3c-8708-0a05c8287ba3";
constexpr const char* kPcToAdvWriteUuid = "7d913c17-e2dd-4c15-b9c2-d1ba4e372ef3";
constexpr const char* kHelloAction = "system.hello";
constexpr const char* kCodexUsageAction = "codex.usage.read";
constexpr const char* kTimeReadAction = "system.time.read";

constexpr const char* kActionsListAction = "actions.list";
constexpr const char* kScriptsExecuteAction = "scripts.execute";
constexpr const char* kShortcutExecuteAction = "actions.shortcut.execute";
constexpr size_t kScriptPageSize = 8;

}  // namespace adv::protocol
