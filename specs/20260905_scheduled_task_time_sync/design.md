> 状态：已确认

# 定时任务与系统授时：技术设计

## 1. 方案与模块

沿用现有 application/core/platform 分层。新增固定容量调度器、共享请求 ID 生成器、系统时间服务和授时控制器；Codex 的长期周期迁入调度器。桌面增加独立系统授时 handler，使用现有路由及响应发送锁。

| 位置 | 修改内容 |
| --- | --- |
| `firmware/src/core/scheduled_task_service.*` | 8 个固定任务槽、查询和生命周期管理 |
| `firmware/src/core/exec_id_generator.*` | 开机生命周期内共享请求序号 |
| `firmware/src/core/system_time_service.*` | 授时有效状态、UTC、偏移、成功时间记录 |
| `firmware/src/platform/system_clock.*` | ESP32 gettimeofday/settimeofday 与 native 替身接口 |
| `firmware/src/application/time_sync/time_sync_controller.*` | capability 门控、授时请求、超时、有限重试 |
| `firmware/src/application/codex/` | 删除旧 RefreshPolicy 周期实现，控制器改用公共服务；页面展示开关 |
| `firmware/src/core/message_codec.*`、`protocol_constants.*` | 授时 action、严格数据解析和请求编码 |
| `firmware/src/platform/ble_transport.*`、`core/outgoing_jsonl_queue.*` | 有界发送队列和逐帧发送，平台只负责 notify |
| `firmware/src/main.cpp` | 任务注册、会话分发、主循环接线、分钟绘制标记 |
| `desktop/src/adv_helper/application/time_module.py` | 被动授时 handler，可注入采样函数 |
| `desktop/src/adv_helper/bootstrap.py`、`core/protocol_constants.py` | 默认注册系统动作、hello capability |
| `protocol/`、双端 tests、相关 README | 新协议示例、兼容说明和回归用例 |

只将需要 native 测试的队列状态机移到 core，不新建通用事件总线、模块框架或多线程调度系统。

## 2. 调度接口与语义

```cpp
enum class ScheduledTaskId { kSystemTimeSync, kCodexUsageRefresh, kDisplayRefresh };
struct ScheduledTaskConfig {
  ScheduledTaskId id;
  uint32_t intervalMs;
  bool enabled;
  bool runImmediately;
};
struct ScheduledTaskSnapshot { bool enabled; uint32_t intervalMs; };
using Callback = std::function<void(uint32_t nowMs)>;

bool registerTask(const ScheduledTaskConfig&, Callback, uint32_t nowMs);
bool getTask(ScheduledTaskId, ScheduledTaskSnapshot&) const;
bool setEnabled(ScheduledTaskId, bool enabled, uint32_t nowMs);
bool updateInterval(ScheduledTaskId, uint32_t intervalMs, uint32_t nowMs);
bool triggerNow(ScheduledTaskId, uint32_t nowMs);
bool rescheduleFromNow(ScheduledTaskId, uint32_t nowMs);
bool cancel(ScheduledTaskId);
void tick(uint32_t nowMs);
```

- 周期范围为 `[1000, 2147483647]` ms；Codex hello 周期仍限制到 1～86400 秒，桌面原配置允许 60～3600 秒，不修改其校验范围。
- 未知/未注册 ID、空回调、重复注册、非法周期、槽满返回 false，不修改原状态；getTask 失败不改变输出。内置枚举后续增加合法 ID，测试通过合法 ID 的注册/释放及测试夹具覆盖满槽，不接受线上任意 ID。
- 使用 `std::array<Slot, 8>`；槽保存 occupied、配置、基准时间、注册顺序和槽代次。取消清空槽，重新注册排在现存任务之后，不能凭物理槽位置改变注册顺序。
- register 先建立完整状态；仅 enabled 且 runImmediately 时调用一次回调。禁用注册忽略立即执行标志，之后不保留待执行动作。
- setEnabled 同值为无副作用成功；false→true 从 now 计时。updateInterval 即使值相同也重设起点；reschedule 只改起点；triggerNow 对暂停任务执行一次但保持暂停。
- tick 对进入时的任务 ID/代次按注册顺序做固定容量快照；每次回调前重查代次、启用状态和到期条件。回调新注册的任务留到下一次 tick，取消或重新注册的旧快照不再执行。
- 执行前更新基准并复制回调，避免回调取消自身销毁正在执行的函数对象。调度器派发期间拒绝嵌套 tick/triggerNow，以及立即执行式注册，返回失败或忽略嵌套 tick；启停、取消、修改周期仍可调用。
- 每次到期将基准设为 now，不追补；全部时间判断复用 elapsedMs。参数 now 必须来自同一轮或更晚的单调时间，不混用消息处理前的旧时间快照。

启动顺序固定为授时 3600000 ms、Codex 300000 ms、显示 60000 ms，均默认启用、不立即执行。显示任务只设置 redrawRequested，不绘图；删除 draw 中独立的分钟到期判断。

## 3. 请求与发送队列

### 3.1 请求 ID

ExecIdGenerator 是 main 持有的单例依赖，注入两个 Controller；使用 64 位递增序号格式化为 16 位十六进制字符串，首次为 1。断线不重置序号；耗尽返回失败，禁止回绕复用。完全重启后 BLE 链路与接收队列重新建立，因此不要求跨开机持久化序号。

桌面 hello 自带的 ID 不属于 ADV 业务在途集合，不拿它作为请求序号。两个 Controller 各自保存 inFlightExecId、requestStartedAtMs；无共享业务锁。

### 3.2 非阻塞队列

将原 sendJsonl 的同步循环改为 `enqueueRequest(execId, jsonl, nowMs)`，成功仅表示队列接受。固定 4 个消息槽，每条上限沿用 4096 bytes（不含最后 LF）；字符串按消息实际长度分配但总槽数和单条长度有界。不接受内嵌换行，统一追加一个 LF。

core 的 OutgoingJsonlQueue 保存 execId、消息、offset、入队时间、发送时间和连接代次；platform 调用 `pollTransmit(nowMs)` 每次最多 notify 一个 20 bytes 分片，距上片至少 10 ms，不补发错过的片数，不调用 delay。完整一条结束后才取下一条，保持 FIFO。

- 尚未开始的消息排队超过 10 秒即丢弃；Controller 自己的请求超时最终释放在途状态，队列拒绝入队则不建立在途。
- `cancelPending(execId)` 删除尚未开始的消息。已经发送部分内容时继续发送余下字节到 LF，不能让下一条拼接到半条 JSON；过期 ID 的响应由 Controller 丢弃。
- 请求超时后调用 cancelPending。断线则立即清空整条发送状态，无需补齐断开的帧。
- BLE 回调仅更新连接状态/代次；主循环发现代次变化先清理收发队列和控制器再处理新会话。每条接收 chunk 标注代次并过滤旧代次，覆盖“断开后快速重连、主循环只看到已连接”的情形，防止旧半帧和缓存进入新会话。
- 单片发送前校验连接代次；实际 notify 是否送达仍无端到端保证，由响应超时处理。逻辑测试使用分片 sink，真机验证实际 BLE 与队列恢复。

### 3.3 超时参数

| 项目 | 参数 | 原因 |
| --- | --- | --- |
| Codex 设备侧总等待 | 210 秒，从成功入队计时 | 当前桌面首次查询依次 initialize/account/read/rateLimits/read；每次 RPC 配置上限 60 秒，另留排队/分片余量 |
| 授时设备侧总等待 | 15 秒，从成功入队计时 | handler 只取本地时间；容纳有界排队及响应传输 |
| 授时短时重试 | 失败后 3 秒，最多再试 2 次 | 每轮最多 3 次，不无限重试 |
| Codex 自动失败重试 | 无额外短时重试 | 等下个周期或 Enter，避免异常高频查询 |

210 秒是设备恢复上限，不承诺所有桌面异常都能在此前完成：当前 RPC 写入/进程创建不全受读取超时覆盖，保留原桌面实现。设备超时不取消已在桌面运行的查询，后续可能收到 BUSY，按现有错误状态处理。

所有匹配须满足 event=response、actionId 正确且 execId 等于当前非空在途 ID。Codex 超时走独立失败入口，保留已有数据且不改变其最近成功接收时间；错误响应同样不应把旧缓存显示成“刚刚更新”。接收非法数据时丢弃，等待有限超时，不提交部分状态。

## 4. Codex 与页面

Controller 注入 scheduler、ID generator、入队/取消回调。request 返回是否成功提交；只检查会话、capability 和 inFlight，不检查页面前台。onKey 仍受当前页面约束。

- 新 BLE 会话第一次有效 hello：应用 Codex 周期，设置支持状态；启用且支持时 triggerNow。连接期间重复相同 hello 不重设周期、不重复首次刷新；有效 hello 配置确有变化才应用变化，身份改变视为新逻辑会话并清空在途与用量。
- 手动 Enter 直接 request；仅入队成功且自动启用时 rescheduleFromNow。周期回调不在响应完成时重设起点。
- Fn+Enter 只改 scheduler 启停；关闭保留在途。Controller tick 仅维护短时状态（超时），不再拥有长期 RefreshPolicy。
- 断线清空 Codex 缓存/在途，保留任务配置和 enabled；新 hello 可更新周期但不能重新开启用户关闭的任务。

CodexPage 接收任务状态快照：周期可整除小时/分钟时显示 h/m，否则显示 s。每种状态均保留底部自动状态行；左侧显示“自动 5m”或“自动已关”，右侧按可用宽度显示缓存年龄/错误/刷新状态。缩短或裁剪次要文字，不覆盖自动状态。空状态中心卡片高度不侵占 y=117 以下页脚。

main 处理开关后立即置脏，不能只比较 inFlight。沿用当前断线屏，不新增断线页面设置入口；全局 Fn 导航先处理，Fn+Enter 不与现有导航冲突。

## 5. 系统时间与桌面协议

SystemClock 提供 bool setUtcMilliseconds(int64_t) 和 bool readUtcMilliseconds(int64_t&)；native 通过 fake 实现，不修改测试宿主机时间。ESP32 平台使用 gettimeofday/settimeofday，秒和微秒转换前先校验范围。

SystemTimeService 保存 hasSynced、lastSuccessfulSyncMs、utcOffsetMinutes。系统设置成功后才提交这些字段；读取未授时时返回不可用。有效负向校时允许，不能用“比当前时间小”判为非法。偏移范围为 -720～840 分钟，Unix 毫秒非负，秒值和转换后的当地时间均须在平台 time_t 范围内，溢出拒绝。

本机安装包元数据为 Arduino framework 3.20017，SDK 头文件标记 IDF 4.4.7；不假设与原方案引用的 IDF 5.2 相同。用 `std::numeric_limits<time_t>` 做真实平台边界，固件构建核对，不因年份范围升级平台。UTC 存入系统；当地时间通过 UTC+偏移后 gmtime_r 转换，不设置全局 TZ，不推算夏令时。

桌面 time_module 用一次 time.time_ns 采样生成 epochMilliseconds，并用相同采样时刻转换当地时间求 utcoffset；每次请求重新计算偏移。采样函数可注入。系统动作直接注册到 ActionRegistry，不经过仅接受 Codex 的用户 actions 配置；不修改 config.json/schema。

请求与成功响应沿用输入方案。Message 增加授时字段，Codec 仅在授时 OK 响应解析时要求严格整数（拒绝字符串、小数、布尔）、数据存在且范围合法；未知字段忽略。结果代码沿用 OK/ERROR/TIMEOUT，不新增协议版本。共享 fixture 新增请求、成功、非法字段；旧 hello 缺 capability 时功能自然降级。

## 6. 授时状态与重连

TimeSyncController 保存 sessionReady、supported、当前电脑、最近成功电脑、inFlight、retryCount、retryPending、retryAt、needsSync 和成功后经过时间。

- 首次有效 hello：未授时或电脑与最近成功来源不同即 needsSync；支持且无在途时开始一轮，triggerNow 并尝试入队。
- 同电脑重连且仍新鲜：不调用 updateInterval/reschedule，不改变已保留的小时任务基准。
- 每轮首次尝试时重设小时任务起点；失败/入队拒绝设 needsSync，安排 3 秒后重试。每次重试无论是否入队均计数；重试直接尝试 request，不改变小时任务基准。
- 成功：写系统时间成功后清除 needsSync/retry 状态，记录成功电脑和时间，rescheduleFromNow，下一次在成功后一小时。
- 三次尝试耗尽后保留 needsSync，等待下一次小时回调或新会话 hello，不在 controller tick 中立即循环发起。
- 小时回调遇断线/无能力只标记 needsSync，不发送、不推进成功记录。重连后 needsSync 使补同步不依赖调度器最近回调基准。
- Controller 每轮更新成功后的经过时间并饱和在 3600000 ms，到期锁存 needsSync，避免断线超过一次 millis 回绕后误判新鲜；必须持续 tick，不假设主循环停转多个回绕。
- 断线取消在途和短时重试；若有未完成请求则保留 needsSync。SystemTimeService 继续提供已授时 UTC 和上次偏移，成功来源也保留。
- 暂停授时任务时不执行后续重试；已在途成功仍可提交时间但不能重新启用任务。首版无授时暂停 UI。

主循环顺序：处理连接代次/接收 → 路由 hello 与响应 → 重新读取 now → 键盘 → 控制器短时维护 → scheduler.tick → 逐片发送 → 读取当前 now 并绘图。hello 先通知授时再通知 Codex；同时到期按注册顺序，已开始的 JSONL 不抢占。

## 7. 公共能力复用评估

| 检索范围 | 现有能力 / 差距 | 处理与影响 |
| --- | --- | --- |
| platform/monotonic_clock、codex/refresh_policy | 已有 elapsedMs，仅 Codex 前台周期 | 复用时钟、新建统一调度，删除旧周期类与对应过时断言 |
| codex_controller | 模块内 32 位 ID | 新建共享 64 位 ID，两个控制器注入使用 |
| core/message_codec、message_router、connection_session | 已有信封、路由、capability/身份 | 扩展字段及会话转移，保留旧动作与版本兼容 |
| platform/ble_transport、core/jsonl_buffer | 接收队列与 JSONL，发送同步阻塞 | 扩展传输层并新建可测有界队列，双动作复用，不复制分片逻辑 |
| codex_usage_state、codex_page、keyboard_adapter | 缓存/中文/UI/Fn 输入 | 扩展超时、响应事件校验和页脚；复用输入，无新键盘层 |
| main draw | 独立分钟周期 | 改成统一任务置脏，保留实际绘制路径 |
| desktop registry/router/bootstrap | 异步 handler 与完整响应锁 | 复用以注册 time handler，不增加第二条发送通道 |
| 固件 core/platform 全部相关时间代码 | 无系统授时能力 | 新建 SystemTimeService + 可替换平台接口，隔离设备系统调用 |

## 8. 实现与验证约束

- 必要注释解释无符号回绕、回调自身取消、半帧不得截断、成功授时基准、超时余量和时区转换；不复述普通赋值。
- 不升级依赖/锁文件，不改 CI、部署或 NVS；不修改已暂存的无关工作。
- native 用假时钟测试服务/控制器/队列；pytest 测试采样、独立注册和并发响应；共享 fixtures 校验双端一致。
- 编排集成覆盖真实 main 顺序对应的 hello、超时、调度、发送和断线事件，不只孤立验证各方法。
- 真机验证组合键、所有页面状态的页脚、同时双动作、断线快速重连、完整一小时同步及漂移、完全断电。测试构建不代表这些人工结果。

| 风险 | 规避与验证 |
| --- | --- |
| 旧时间快照触发无符号下溢 | 消息处理后重新取 now；假时钟测试同轮响应后调度 |
| 通信误差大于原估计 | 真机记录首次误差与同步前误差，不承诺 0.1～0.5 秒 |
| 快速重连保留旧消息 | 连接代次贯穿接收和发送状态，故障注入覆盖 |
| 回调修改槽或递归 | 快照代次、回调副本、派发保护覆盖取消重注册和嵌套调用 |
| 队列超时截断 JSON | 只删除未发消息；活动帧完成后丢弃过期响应 |
| 慢 Codex 查询误触发设备超时 | 210 秒预算覆盖配置上限的三次 RPC；仍验证桌面 BUSY 恢复 |

## 9. 需求追踪

| 需求 | 设计覆盖 |
| --- | --- |
| R-01 | 第 2 节任务接口、固定容量、全部生命周期 |
| R-02 | 第 2、6 节单调调度、分钟置脏、循环时序 |
| R-03 | 第 4 节 Codex 后台/能力/hello/手动起点 |
| R-04 | 第 4 节开关、缓存、页脚、重绘 |
| R-05 | 第 5 节默认 handler、协议与兼容 |
| R-06 | 第 5、6 节平台时间、校验、有效状态与来源 |
| R-07 | 第 6 节成功周期、到期锁存、重试与重连 |
| R-08 | 第 3、6 节共享 ID、在途、超时与隔离 |
| R-09 | 第 3 节有界队列、分片与取消 |

## 待确认问题

无。需求范围保持不变；上述参数和实现方案随技术设计整体确认后进入任务拆解。
