# 固件代码说明

C++17、Arduino ESP32、PlatformIO，目标为 Cardputer-Adv（ESP32-S3）。依赖声明见 [platformio.ini](platformio.ini)，M5Cardputer 使用 Git tag `1.2.0`；实际解析版本以构建输出为准。构建、刷写和按键操作见 [项目 README](../README.md)。

## 结构与职责

| 位置 | 职责 |
| --- | --- |
| `src/main.cpp` | 实例装配、任务注册、消息路由和主循环 |
| `src/application/` | 页面外壳、Codex 页面/缓存/控制器、授时控制器 |
| `src/core/` | 调度、会话、共享请求 ID、协议、发送队列与系统时间服务 |
| `src/platform/` | BLE、键盘、屏幕、单调时钟与系统 UTC 平台适配 |
| `test/` | Unity native 用例；假时钟及分片 sink 替代硬件 |

页面负责输入和显示，控制器负责请求及状态，平台层处理硬件调用。新增页面复用 `AppShell`、`NavigationService`、`DisplayAdapter`；长期后台周期注册到 `ScheduledTaskService`。

## 主循环与会话

执行顺序为 BLE 接收及代次处理 → 消息路由 → 重新读取单调时间 → 键盘 → 控制器短时维护 → 调度器 tick → 单片发送 → 绘制。

BLE 回调只提交字节和连接状态，业务逻辑在主循环串行处理。消息处理后重新读取时间，避免响应记录的新时间与循环旧快照相减发生无符号下溢。分钟显示任务只置脏，实际绘制仍在主循环。

BLE 已连接不代表业务就绪，必须接受合法 hello。重复 hello 不重复首次请求；hello 不包含本地刷新配置，不能覆盖文件周期或重新开启用户关闭的自动任务。身份变化或连接代次变化会清空旧请求与 Codex 缓存；系统走时及自动开关保留。

## 调度与请求

`ScheduledTaskService` 使用固定 8 槽，按注册顺序调度；到期每次最多执行一次，不补跑错过的周期。周期、超时与重试都使用 `millis()` 单调时间，不受日历校时影响。

| 注册顺序 | 周期 | 行为 |
| --- | --- | --- |
| `kSystemTimeSync` | 3600000 ms | 成功授时后重设下一小时起点 |
| `kCodexUsageRefresh` | 默认 300000 ms | 由 CodexConfigService 应用本地文件，切页仍执行 |
| `kDisplayRefresh` | 60000 ms | 更新电量及相对时间显示的脏标记 |

任务服务提供注册、查询、启停、改周期、立即执行、重设起点和取消。恢复启用等待完整周期；立即执行暂停任务不恢复自动状态。回调代次快照支持自取消，派发期间拒绝递归执行；回调必须快速返回。

两个控制器共享 `ExecIdGenerator`，各自最多一个在途请求。Codex 入队成功的手动查询才重设周期；响应失败保留旧缓存和成功年龄。授时按最近成功状态处理同电脑重连，到期、失败或更换电脑时补同步。

| 等待/恢复 | 参数 |
| --- | --- |
| Codex 请求 | 210 秒总等待，无短时自动重试 |
| 授时请求 | 15 秒总等待；失败后间隔 3 秒最多再试 2 次 |
| 未开始发送的消息 | 排队超过 10 秒丢弃，控制器由请求超时释放在途 |

`OutgoingJsonlQueue` 固定 4 槽，整条 JSONL 按 FIFO 发送；每次 poll 最多一个 20-byte 分片，间隔至少 10 ms。取消只能移除未发送消息，半帧必须继续到 LF；断线则全部清空。参数与协议约束见 [共享协议](../protocol/README.md)。

## 模块运行时配置

`application/codex/codex_config.*` 定义唯一默认值、严格 JSON 校验和 `CodexConfigService`；`platform/config_file_store.*` 隔离 LittleFS 挂载及 POSIX VFS 文件操作。运行文件为 `/config/codex.json`，默认镜像样例为 `data/config/codex.json`。

`read()` 每次读文件；`reload()` 读取并应用；`save(json)` 校验、比较、写入或跳过同值、重读并应用。调度器持有当前周期，绘制和 tick 不读文件。应用在 I/O 后采样单调时间，只有周期变化才 updateInterval；不改变 enabled、在途请求和缓存。

文件限制 512 bytes，只接受整数 `refreshIntervalSeconds`（60～3600），拒绝未知字段和尾部额外内容。写入 `/config/codex.json.tmp` 后重读核对，通过 rename 替换正式文件，禁止先删除旧文件；检查 stdio 写入/flush/close 错误。正式文件重读或应用失败时保留旧运行周期并明确报告，不能把文件已保存当作已生效。

`CodexConfigCommands` 实现固定串口命令，行缓冲最大 576 bytes，每轮最多读取 64 bytes；超长行丢弃到换行，保留 `t` 诊断。主循环在消息处理后、tick 前调用，完成后重新取 now；配置变化置脏。未来 settings 直接调用相同服务，读取时展示文件值，保存时根据状态/changed 反馈并重绘。

LittleFS 使用原分区标签 `spiffs`，只设置 `board_build.filesystem = littlefs`。挂载使用 `begin(false)`，不自动格式化；普通读取不创建文件。首次部署与操作命令见 [项目 README](../README.md#adv-模块配置)，设备边界见 [配置规格的验收步骤](../specs/20260905_adv_module_runtime_config/tasks.md#task-08-真机配置持久化与即时生效验收)。

## 系统时间与诊断

`SystemTimeService` 校验 UTC、偏移和平台 `time_t` 范围；平台设置成功后才提交授时状态。`PlatformSystemClock` 使用 `gettimeofday/settimeofday`，native 测试注入 fake，不修改宿主时间。

UTC 不预先加时区偏移；当地时间通过 UTC 加最近一次偏移转换，不自行推算夏令时。系统断线后继续走时，开机首次成功前时间不可用。授时成功状态只表示已接受电脑时间。

串口发送小写 `t` 按需输出 `synced`、`utc_ms`、`offset_min`、`last_sync_ms`、`now_ms`。后两者是单调毫秒，不能与 UTC 直接相减；`synced=0` 时 UTC 输出不可用。该入口只作本机诊断，无周期任务或新增 BLE action。

## 开发验证

在项目根目录执行：

```sh
pio test -d firmware -e native
```

native 覆盖调度生命周期、回绕、控制器恢复、协议及双任务集成；目标构建检查 Arduino API 与接线。设备布局、组合键、实际 BLE 和时间测量方法见 [授时规格的验收步骤](../specs/20260905_scheduled_task_time_sync/tasks.md#task-15-操作说明与真机验收)。设计历史见 [定时任务与授时规格](../specs/20260905_scheduled_task_time_sync/)。
