> 状态：已确认

# ADV 模块运行时配置：项目上下文

## 目标与来源

用户要求 Codex 刷新周期完全由 ADV 控制，默认 300 秒，采用模块独立的设备运行时 JSON 文件；后续 settings 模块可修改各模块配置并立即生效。希望需要配置时读取最新文件，不使用编译期配置替代。

本规格接续已实现的 `20260905_scheduled_task_time_sync`，只调整配置归属及运行时配置能力，旧规格保留为历史。

## 当前实现

- 固件为 C++17、Arduino ESP32、PlatformIO，使用 ArduinoJson；desktop 为 Python/asyncio。
- `CodexController::begin()` 注册 300000 ms 周期任务；`main.cpp` 每轮调用统一调度器。
- desktop `config.py` 的 `refreshIntervalSeconds` 经 hello 下发，固件 Message/ConnectionSession 保存并由控制器更新周期。这条链路需移除。
- desktop `requestTimeoutSeconds` 默认 15 秒，用于 App Server 单次 RPC 响应读取；ADV 的 210 秒为整次设备请求等待上限，二者职责不同。
- 目前未发现固件文件配置、文件系统挂载、模块配置存储或配置变更通知能力。
- 现有 Fn+Enter 启停只保存在本次开机；本次新增文件先只存刷新周期，不自动扩大到开关持久化。

## 公共能力复用

| 能力 | 当前情况与处理方向 |
| --- | --- |
| ScheduledTaskService | 复用 updateInterval/getTask；改周期从当前单调时间重设起点，不改变启停 |
| CodexController / CodexPage | 扩展配置应用入口；页脚继续读取实际任务周期，复用置脏机制 |
| MessageCodec / ConnectionSession | 删除刷新周期的业务解释；保留 hello 的版本、身份、capabilities |
| ArduinoJson | 复用解析能力；设备配置与 BLE 信封分别校验 |
| platform 分层及 native fake | 文件操作隔离平台层，验证不依赖真实 Flash |
| 配置存储和保存入口 | 现有能力缺失；新增最小文件读写及模块应用路径，供未来 settings 复用 |

## 设计阶段需落实的边界

- 选择设备存储介质、运行时路径、文件初始化和部署方式。优先评估无需 SD 卡的片内文件系统；不能未经验证假定现有分区可用。
- 文件重新读取不足以保证周期立即更新：如果只在旧任务到期后读取，缩短周期仍会等待旧截止时间；必须有保存后的配置应用入口。
- 不能每轮无条件 updateInterval，否则起点反复重设，任务可能永不到期。
- “配置读取调用获取当前文件”与“调度器持有当前运行周期”分开；后者是运行状态，并非第二份持久配置来源。
- 本阶段只新增规格文档，不改生产代码、依赖、分区或历史暂存内容，不运行构建和设备操作。
