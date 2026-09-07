# 固件代码说明

C++17、Arduino ESP32、PlatformIO，目标为 Cardputer-Adv（ESP32-S3）。依赖声明见 [platformio.ini](platformio.ini)，M5Cardputer 使用 Git tag `1.2.0`；实际解析版本以构建输出为准。构建、刷写和按键操作见 [项目 README](../README.md)。

## 结构与职责

| 位置 | 职责 |
| --- | --- |
| `src/main.cpp` | 实例装配、任务注册、消息路由和主循环 |
| `src/application/` | 页面外壳、Codex/脚本/设置页面和控制器、公共 Wi-Fi 服务、授时、模块配置 |
| `src/core/` | 输入路由、串口分发、调度、会话、请求 ID、协议、发送队列与系统时间服务 |
| `src/platform/` | BLE、Wi-Fi、键盘、屏幕、文件系统、单调时钟与系统 UTC 平台适配 |
| `test/` | Unity native 用例；假时钟及分片 sink 替代硬件 |

页面负责输入和显示，控制器负责请求及状态，平台层处理硬件调用。新增页面复用 `AppShell`、`NavigationService`、`DisplayAdapter`；长期后台周期注册到 `ScheduledTaskService`。

## 主循环与会话

执行顺序为 BLE 接收及代次处理 → 消息路由 → 串口配置分发 → 重新读取单调时间 → 键盘 → 重新读取单调时间 → Wi-Fi tick → 重新读取单调时间 → 控制器短时维护 → 调度器 tick → 单片发送 → 绘制。

BLE 回调只提交字节和连接状态，业务逻辑在主循环串行处理。消息处理后重新读取时间，避免响应记录的新时间与循环旧快照相减发生无符号下溢。分钟显示任务只置脏，实际绘制仍在主循环。

BLE 已连接不代表业务就绪，必须接受合法 hello。重复 hello 不重复首次请求；hello 不包含本地刷新配置，不能覆盖文件周期或重新开启用户关闭的自动任务。身份变化或连接代次变化会清空旧请求、Codex 缓存与脚本目录/反馈；系统走时及自动开关保留。

## 调度与请求

`ScheduledTaskService` 使用固定 8 槽，按注册顺序调度；到期每次最多执行一次，不补跑错过的周期。周期、超时与重试都使用 `millis()` 单调时间，不受日历校时影响。

| 注册顺序 | 周期 | 行为 |
| --- | --- | --- |
| `kSystemTimeSync` | 3600000 ms | 成功授时后重设下一小时起点 |
| `kCodexUsageRefresh` | 默认 300000 ms | 由 CodexConfigService 应用本地文件，切页仍执行 |
| `kDisplayRefresh` | 60000 ms | 更新电量及相对时间显示的脏标记 |

任务服务提供注册、查询、启停、改周期、立即执行、重设起点和取消。恢复启用等待完整周期；立即执行暂停任务不恢复自动状态。回调代次快照支持自取消，派发期间拒绝递归执行；回调必须快速返回。

Codex、授时、脚本控制器共享 `ExecIdGenerator`。Codex 和授时各自最多一个在途请求；脚本分别最多一个目录请求和一个执行请求。首次合法 hello 按授时、Codex、脚本第一页顺序提交；目录和执行能力分别检查。Codex 入队成功的手动查询才重设周期；响应失败保留旧缓存和成功年龄。授时按最近成功状态处理同电脑重连，到期、失败或更换电脑时补同步。

| 等待/恢复 | 参数 |
| --- | --- |
| Codex 请求 | 210 秒总等待，无短时自动重试 |
| 授时请求 | 15 秒总等待；失败后间隔 3 秒最多再试 2 次 |
| 脚本目录 | 15 秒总等待，失败后 Enter 显式重读目标页 |
| 脚本执行 | 45 秒总等待，不自动重试；超时为结果未确认 |
| 未开始发送的消息 | 排队超过 10 秒丢弃，控制器由请求超时释放在途 |

`OutgoingJsonlQueue` 固定 4 槽，整条 JSONL 按 FIFO 发送；每次 poll 最多一个 20-byte 分片，间隔至少 10 ms。取消只能移除未发送消息，半帧必须继续到 LF；断线则全部清空。参数与协议约束见 [共享协议](../protocol/README.md)。

## 模块运行时配置

`application/codex/codex_config.*` 定义唯一默认值、严格 JSON 校验和 `CodexConfigService`；`platform/config_file_store.*` 隔离 LittleFS 挂载及 POSIX VFS 文件操作。运行文件为 `/config/codex.json`，默认镜像样例为 `data/config/codex.json`。

`read()` 每次读文件；`reload()` 读取并应用；`save(json)` 校验、比较、写入或跳过同值、重读并应用。调度器持有当前周期，绘制和 tick 不读文件。应用在 I/O 后采样单调时间，只有周期变化才 updateInterval；不改变 enabled、在途请求和缓存。

文件限制 512 bytes，只接受整数 `refreshIntervalSeconds`（60～3600），拒绝未知字段和尾部额外内容。写入 `/config/codex.json.tmp` 后重读核对，通过 rename 替换正式文件，禁止先删除旧文件；检查 stdio 写入/flush/close 错误。正式文件重读或应用失败时保留旧运行周期并明确报告，不能把文件已保存当作已生效。

`ConfigCommandDispatcher` 独占串口读行，按前缀交给 `CodexConfigCommands` 或 `InputConfigCommands`；行缓冲最大 576 bytes，每轮最多读取 64 bytes；超长行丢弃到换行，保留 `t` 诊断。主循环在消息处理后、tick 前调用，完成后重新取 now；配置变化置脏。SettingsController 直接调用同一 CodexConfigService，展示保存与运行状态，不复制调度逻辑。

LittleFS 使用原分区标签 `spiffs`，只设置 `board_build.filesystem = littlefs`。挂载使用 `begin(false)`，不自动格式化；普通读取不创建文件。首次部署与操作命令见 [项目 README](../README.md#adv-模块配置)，设备边界见 [配置规格的验收步骤](../specs/20260905_adv_module_runtime_config/tasks.md#task-08-真机配置持久化与即时生效验收)。

## 按键与方向映射

键盘每轮读取第一层物理键快照，不依赖只比较键数量的 `isChange()`。主键新按下产生一次事件；主键保持按下时增减修饰键不重复执行；同次新按下多个主键全部忽略，需释放重按。Fn+Enter 保留物理身份。

输入统一经 `InputRouter`：Fn 系统组合 → Alt 全局动作 → 设置文本编辑上下文（启用时）→ 当前模块方向映射 → 页面输入。未知 Fn 组合被消费，只有 Codex 页的纯 Fn+Enter 开关自动刷新；Fn+Alt+数字仍切页，Fn+Alt+Enter 不响应。Alt 只接受没有 Ctrl/Shift/Opt 的单个字母；Caps Lock 不改变物理匹配，普通字母不执行。

无修饰 `; , . /` 默认分别映射上、左、下、右。Codex 上下滚动，脚本上下选择并跨页；设置页左右调亮度，Codex 周期左右调整，网络信息页方向键翻页。Fn+`,` / `/` 切栏，Fn+`;` / `.` 为系统保留组合，不滚动页面。剪贴板保留共同输入约定和全局快捷键。设置编辑使用独立文本字符，支持 Shift；`; , . /` 和数字作为输入，不用于导航，Fn/Alt 仍优先。

`/config/input.json` 为四模块各自配置，默认文件在 `data/config/input.json`。以下是串口命令，逐行发送且带换行：

```text
input.config.read
input.config.save {"scripts":{"directionMapping":false}}
input.config.reload
input.config.save {}
```

第二行只关闭脚本映射；最后一行恢复四模块默认开启。save 为完整替换语义，省略模块/字段均恢复 true，不是合并当前值。模块名为 `codex`、`scripts`、`clipboard`、`settings`；各模块只接受 boolean `directionMapping`，拒绝未知字段、非法类型及尾部垃圾，整份 JSON 最多 512 字节。

保存复用 ConfigFileStore 的临时文件核对和替换，再重读应用；相同有效配置不重复写盘。`read` 输出文件值和 `active`，不修改运行值；`reload` 重读后应用。启动读取失败保留默认，运行中失败保留上一有效配置；`ReloadFailed` 表示文件可能已保存，可修复后 reload。普通按键和绘制不访问文件。无文件系统时不格式化，部署及覆盖边界见项目 README。

## 设置操作

`Fn+4` 进入，设置输入和绘制位于 BLE 离线拦截前。首页有“屏幕亮度”“Wi-Fi”“Codex自动刷新”。列表用上下或 Tab 选择，Enter 确认，Backspace 返回；关闭 `settings.directionMapping` 后仍可用 Tab/Enter/Backspace 完成操作。

| 功能 | 操作及结果 |
| --- | --- |
| 亮度 | 左右逐档调整，边界停止；Tab 循环五档。20% / 40% / 60% / 80% / 100% 对应 51 / 102 / 153 / 204 / 255，默认第三档。调整立即应用并保存，同值不重复写入；失败保留当前亮度并显示错误，可再次调整重试 |
| Codex 周期 | 当前页左右或上下调整 1～60 分钟，Tab 循环递增；停止调节 600 毫秒后自动保存，返回或切换模块时补存。失败保留草稿和错误，再调整或离开重试；已有 90 秒等非整分钟值按秒显示，首次调整前不覆盖 |
| Wi-Fi 字段 | SSID、用户名、密码均明文；Enter 在当前行编辑，Enter 写回内存草稿并结束编辑，Tab 写回后直接编辑下一字段，尚未写 Flash。支持大小写、数字、空格、符号；退格删除整个 UTF-8 字符；不裁剪首尾空格 |
| 查看网络信息 | 汇总保存状态、扫描与测试结果、上次测试 IP 和无线状态；不提供凭据全文查看。Tab 循环翻页，方向键逐页移动，Backspace 返回；关闭失败时显示“重试关闭Wi-Fi”，Enter 执行，否则 Enter 返回 |
| 扫描网络 | 选择“扫描网络”，完成后 Enter 选中 SSID，只改 SSID，不改用户名/密码、不保存或连接。列表按信号排序、同名去重，最多 32 项；截断、失败或无结果可查“查看扫描结果”，也可“返回手动配置” |
| 保存 | 校验当前草稿后保存一组 Wi-Fi 配置，不启用无线；格式合法的错误密码也可保存。保存失败保留旧有效快照，结果在操作行短暂显示，详细反馈见“查看网络信息” |
| 测试连接 | 使用当前草稿，不自动保存。认证完成且取得有效 IP 才成功，不探测公网；测试结果和 IP 是历史记录，修改凭据后提示重新测试，测试值与保存值不同时提示未保存 |
| 重试关闭 | 设置自己的扫描/测试关闭失败时进入“查看网络信息”选择“重试关闭Wi-Fi”。其他模块持有连接时返回忙，不抢占或替其关闭 |

SSID 为 1～32 字节有效 UTF-8，用户名 0～64 字节；禁止控制字符。用户名为空走普通认证：密码可为空（开放网络），否则为 8～63 个可打印 ASCII，或 64 位十六进制 PSK。用户名非空走无证书 PEAP，账号同时作为 identity 和 username，密码为 1～64 字节有效 UTF-8。没有网络类型选择或中文输入法；可扫描带入中文 SSID。编辑达到字节上限会拒绝新增字符，不截断原值。

Wi-Fi 固定 AUTO：开机只加载配置，保存也不连接；扫描截止 15 秒，连接截止 30 秒，均为非阻塞状态机。扫描和设置测试终态释放并关闭 Wi-Fi，保留结果；无法确定认证失败原因时显示超时，不据此断言密码错误。切页、BLE 断线保留草稿/编辑状态，已启动操作继续；重启只恢复保存值。脚本短暂反馈消失后恢复页面正文。首页仅保留一行操作提示，子页不设置固定提示栏；长字段编辑时随尾部光标横向滚动。

| 配置文件 | 默认及恢复 |
| --- | --- |
| `/config/display.json` | `{"brightnessLevel":3}`，缺失/损坏时开机使用 60% |
| `/config/wifi.json` | `{"ssid":"Example","username":"","password":""}`，缺失/损坏时开机未配置；示例是开放网络 |
| `/config/codex.json` | `{"refreshIntervalSeconds":300}`，缺失/损坏时开机使用 5 分钟 |

三项独立保存，不相互重置。Codex 改周期从生效时刻等待完整周期；同值不重设计时，不改变自动开关/缓存/在途请求。`Fn+Enter` 仍仅在 Codex 页生效，其开关重启恢复默认开启。

“已保存”“保存失败”“文件可能已保存，读取失败”“已保存但应用失败”含义不同，后两者应修复存储/应用问题后重试；亮度失败时仍保持当前生效档位。运行中读失败保留已有有效值并提示。保存复用临时文件写入、校验和 rename，不自动格式化。已有 LittleFS 首次保存即可创建新增文件；仅首次尚未部署文件系统时按根 README 部署 `firmware/data/`，`uploadfs` 覆盖整个分区，普通 `upload` 在未擦除/未变更分区时保留配置。

## 公共 Wi-Fi 接口

`application/wifi/WifiService` 独立于页面。main 只装配一个实例，SettingsController::tick 由主循环调用并驱动该实例；未来模块使用同一实例，不再单独 tick 或直接调用平台 WiFi API。配置查询不启用无线，没有业务需求时不连接、不重试、不按帧读文件。

| 接口 | 契约 |
| --- | --- |
| `configurationStatus()` | 已加载配置的 ConfigStatus；不读文件、不暴露凭据、不保证可连 |
| `canConnect()` | Ready / NotConfigured / InvalidConfig / StorageError / Busy / ReleaseFailed / IdsExhausted；Ready 只表示允许尝试 |
| `connect()` | 发起前重新读取已保存配置，返回 WifiRequest；拒绝时 requestId 为 0，接受时为非零 uint64_t，连接异步完成 |
| `status(requestId)` | 返回该请求的 phase、outcome、SSID/IP；过期或 0 身份为 expired。仅当前/最近一次请求保留于服务 |
| `isConnected()` | 当前实际连接快照具有有效 IP，不以历史成功判定在线 |
| `close(requestId)` | 仅本次持有者可取消/关闭；成功为 kOff，关闭失败为 kReleaseFailed，保留身份供显式重试 |
| `scan()` / `test(draft)` | 同一互斥服务；扫描不连接，测试不保存，两者终态自动关闭；模块 connect 成功保持连接 |
| `scanResults()` | 最多 32 项，含 count/truncated；下一次扫描会替换，需长期保留时自行复制 |
| `tick(nowMs)` | 由既有主循环推进阶段和超时，与页面及 BLE 会话无关 |

典型调用分三个时机，下列为使用同一 `wifi` 实例的片段：

```cpp
// 1. 用户或业务明确请求时执行一次。Ready 是提示，connect 才做最终检查。
uint64_t requestId = 0;  // 调用模块持有，不使用 BLE execId。
if (wifi.canConnect() == adv::WifiAvailability::kReady) {
  const auto request = wifi.connect();
  requestId = request.requestId;
  // requestId == 0：按 request.availability 显示原因，本次不继续业务。
}
```

```cpp
// 2. 后续循环查询，不能 while 等待。主循环已经驱动 wifi.tick。
const auto state = wifi.status(requestId);
if (!state.expired && state.phase == adv::WifiPhase::kConnected && wifi.isConnected()) {
  // 调用模块可推进自己的非阻塞业务；成功连接不会因 30 秒截止而被关闭。
}
// kConnecting：继续等待；kOff：检查 outcome 区分失败/超时/取消等。
// kReleaseFailed：停止业务，保留 requestId，等待显式重试关闭。
// expired：请求已不属于自己，清除本地身份，不关闭其他调用者的新请求。
```

```cpp
// 3. 业务完成、出错或用户取消时，显式关闭；关闭失败后的重试也走这里。
if (requestId != 0) {
  const auto closed = wifi.close(requestId);
  if (closed.expired || closed.phase == adv::WifiPhase::kOff) requestId = 0;
  // kReleaseFailed 时保留身份并报告关闭失败，不在每帧无间隔重试。
}
```

阶段为 `kOff / kScanning / kConnecting / kConnected / kReleasing / kReleaseFailed`，结果为 `kNone / kSucceeded / kFailed / kTimedOut / kDisconnected / kCancelled`。关闭不会覆盖结果，成功测试可以同时为 `kOff + kSucceeded`，此时历史 IP 不代表当前在线。模块连接失败、超时、使用中断线自动清理，无后台重连；模块成功后有显式关闭责任。

单一使用者互斥，不抢占、不排队；保存新配置不打断在途连接，下一次 connect 使用新值。同一已关闭身份重复关闭不再操作平台，旧身份不能关闭新连接，身份耗尽拒绝分配。公开接口没有永久联网开关，本期没有真实业务模块使用 Wi-Fi。

设备验收及尚未覆盖的行为见 [设置验收记录](../specs/20260906_settings_module/acceptance.md)。

## 脚本页和全局反馈

`ScriptsController` 只保存当前页最多 8 条及分页元数据，`ScriptsPage` 展示其中最多 4 行，使选中项保持可见。全表首尾停止；跨页加载期间忽略方向输入且 Enter 不执行旧页；加载失败后 Enter 只重读目标页，成功后再次 Enter 才执行。切栏保留页、选中位置及在途执行，断线或身份变化清空。

页面展示 `effectiveKey`，重复 key 的后项仍可选中执行。名称按实际字体宽度沿 UTF-8 字符边界裁剪，原名称和 actionId 不变。加载中、空目录、失败、旧电脑未支持均有中文状态。

执行中反馈跨页持续显示，结果/本地错误在 3 秒后消退；AppShell 在内容底部临时覆盖页面页脚，消退时恢复原页脚，不占用顶栏电量/连接区。超时和消退使用单调时间，不注册新增周期任务；主循环在状态变化时重绘。

更多操作及实际结果记录见 [脚本与快捷键人工验收](../specs/20260905_scripts_global_shortcuts/acceptance.md)。

## 系统时间与诊断

`SystemTimeService` 校验 UTC、偏移和平台 `time_t` 范围；平台设置成功后才提交授时状态。`PlatformSystemClock` 使用 `gettimeofday/settimeofday`，native 测试注入 fake，不修改宿主时间。

UTC 不预先加时区偏移；当地时间通过 UTC 加最近一次偏移转换，不自行推算夏令时。系统断线后继续走时，开机首次成功前时间不可用。授时成功状态只表示已接受电脑时间。

串口发送小写 `t` 按需输出 `synced`、`utc_ms`、`offset_min`、`last_sync_ms`、`now_ms`。后两者是单调毫秒，不能与 UTC 直接相减；`synced=0` 时 UTC 输出不可用。该入口只作本机诊断，无周期任务或新增 BLE action。

## 开发验证

在项目根目录执行：

```sh
pio test -d firmware -e native
```

native 覆盖调度生命周期、回绕、输入快照/路由、配置、脚本分页/显示数据、控制器恢复、协议及多模块协作；目标构建检查 Arduino API 与接线。设备布局、组合键、实际 BLE 和时间测量方法见 [授时规格的验收步骤](../specs/20260905_scheduled_task_time_sync/tasks.md#task-15-操作说明与真机验收)。设计历史见 [定时任务与授时规格](../specs/20260905_scheduled_task_time_sync/)。
