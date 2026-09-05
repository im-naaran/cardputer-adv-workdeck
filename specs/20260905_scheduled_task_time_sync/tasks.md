> 状态：已确认

# 定时任务与系统授时：任务拆解

## 提交前人工检查反馈（2026-09-05）

用户反馈已按本地文档的相应步骤完成 ADV 等检查，并决定不提交本地准备材料和独立验收清单。正式需求、操作及验收关注点以本规格和 README 为准。该反馈未附逐项结果或时间测量数值，因此不补录具体精度或故障注入结论；下方原执行记录和未勾选验收项保留其证据边界。一期前台刷新策略已由后续后台调度及设备本地配置规格替代。

## 本轮执行范围与验证记录（2026-09-05 续传）

用户要求继续剩余任务，执行 task-13～15。task-13、14 的实现与自动化已完成；task-15 操作说明和测量入口已完成，真机验收待用户执行，保留未勾选。

- 主循环按授时、Codex、分钟显示顺序注册任务，先维护控制器再统一调度和逐片发送；重复 hello 保持幂等，身份变化清理旧请求。
- 固件全量 native 54 项无失败；桌面 `uv run --locked python -m pytest` 88 项无失败。
- 设备构建完成，Flash 1490249 bytes / RAM 50380 bytes；未刷写。`git diff --check` 无报错；额外检查全仓库暂存内容时发现原有文档行尾空白及部分文件末尾空行，未改动这些无关内容。
- 固件、桌面与协议 README 已更新；串口发送 `t` 可按需读取授时状态和 UTC，用于真机测量。
- 未执行刷写和真机验收；自动化不代表屏幕、物理组合键、真实 BLE 或时间精度已经确认。

## task-09～12 执行记录（历史）

用户本轮指定“task9 10 11 12”。已完成 task-09～12 的实现与定向自动化验证，停在 task-12；task-13～15 保留未执行。

- Codex 已使用共享任务服务与请求 ID，切页后台刷新、210 秒请求超时、Fn+Enter 自动开关和页面状态展示已接入。
- TimeSyncController 已实现首次/小时授时、15 秒超时、3 秒间隔最多 2 次重试，以及重连/换电脑恢复；用假时钟和模拟请求验证。授时实例、hello/response 路由及主循环调用仍待 task-13 接入，当前设备不会自动授时。
- 定向 native：Codex 6 项、授时 9 项、页面 7 项、导航 4 项、系统时间 2 项，共 28 个不同用例无失败。按任务指定范围执行，未重复桌面全量。
- 设备构建完成，Flash 1486901 bytes，RAM 50228 bytes；git diff --check 无报错。未刷写，Fn+Enter 物理组合键、页脚布局和真实授时行为未人工确认。
- 原 RefreshPolicy 两个文件已由统一调度替代并删除，可从用户原暂存内容恢复；未执行 Git 暂存或提交操作。

## 上轮 task-01～08 验证记录

用户在执行中明确要求“先做到 task08”。上轮仅完成 task-01～08；已开始的 task-09 及后续代码已撤回，不继续执行 task-09～15。任务整体保留为后续计划，完成勾选指实现与自动化结果，不代表真机验收。

- 固件全量 native：38 项无失败；随后补充回调边界及当地时间断言，定向重跑 6 项无失败（当时共 39 个用例）。
- 桌面：`cd desktop && uv run python -m pytest`，88 项无失败。
- 设备：`pio run -d firmware` 完成，Flash 1482653 bytes，RAM 49820 bytes；未刷写或真机测试。
- 桌面 pytest 脚本 shebang 仍引用旧项目路径，使用 `uv run python -m pytest` 绕过；`uv sync --locked` 未更改锁文件。后续测试命令建议使用该模块入口。
- 实际构建选用 framework-arduinoespressif32 3.20016.0（Arduino 2.0.16）；设计阶段读取的另一个已安装包版本不能代表本工程实际解析版本。平台时间范围按实际编译的 time_t 校验，未升级依赖。
- 上轮可观察变化：BLE 发送入队、桌面 hello 多出系统授时能力；当时未实现后台刷新和控制器。

## 执行约束

- 依据已确认 requirements.md 和 design.md；本清单确认后才进入实现。
- 每项按约 30 分钟以内的实现单元拆分；真机等待一小时单独列为验收任务，实际耗时如超过预估如实记录。
- 按编号串行推进可避免共享源码冲突；依赖只指向较小编号。各任务先完成自己的逻辑验证，再更新状态和 changelog。
- 每项形成独立可审阅差异，不自动提交 Git。回滚已接入的基础能力时先逆序撤回依赖任务，保留用户原有改动；新增未接入能力可单独撤回。
- 过渡适配仅用于保持中间步骤可编译，最终在 task-13 清除；不保留重复周期或阻塞发送实现。
- 命令中的 pio 若不在 PATH，可使用本机 /Users/naaran/.platformio/penv/bin/pio；桌面命令在 desktop 目录使用 uv。不升级依赖、锁文件或 CI。
- 每项默认待确认问题为“无”；发现与设计不符的公共能力缺口时先更新设计/任务再实现。

## 任务清单

- [x] task-01: 统一调度器基础接口
- [x] task-02: 调度回调边界与回绕
- [x] task-03: 共享请求 ID 生成器
- [x] task-04: 有界 JSONL 发送队列
- [x] task-05: BLE 队列接入与连接代次隔离
- [x] task-06: 授时协议与共享样例
- [x] task-07: 桌面默认系统授时能力
- [x] task-08: 系统 UTC 服务及平台适配
- [x] task-09: Codex 控制器迁移与超时恢复
- [x] task-10: 授时控制器首次同步与小时周期
- [x] task-11: 授时重试及重连恢复
- [x] task-12: Codex 自动状态页脚与开关
- [x] task-13: 主循环双任务与会话编排
- [x] task-14: 双任务集成与回归验证
- [ ] task-15: 操作说明与真机验收

## task-01: 统一调度器基础接口

本轮结果（2026-09-05）：已实现固定 8 槽及全部生命周期接口；test_scheduled_task 覆盖接口/参数/容量/启停边界。

追踪需求：R-01、R-02

依赖任务：无

修改范围：firmware/src/core/scheduled_task_service.*；firmware/test/test_scheduled_task/test_main.cpp

公共能力处理：新建调度器；已检索 RefreshPolicy 和 monotonic_clock，复用 elapsedMs，现有实现缺少全局多任务生命周期。

完成标准：实现固定 8 槽、合法 ID 校验、周期范围、查询、注册/取消、启停/改周期/立即执行/重设起点；按设计返回失败且不污染原状态。满槽测试可用 native 专用夹具访问槽状态，不新增虚假生产任务。

代码注释要求：解释周期边界、重复启用无副作用及注册时间基准。

自动化验证：假时钟覆盖全部接口、非法参数、满槽、重复 ID、暂停任务单次执行、恢复等待完整周期。

执行命令：`pio test -d firmware -e native -f test_scheduled_task`

人工验证关注点：无单独 UI；实际周期由 task-15 真机确认。

待确认问题：无。

## task-02: 调度回调边界与回绕

本轮结果（2026-09-05）：已实现回调快照代次、自取消保护、注册顺序及回绕；追加 immediate_order_and_callback_updates 后该测试组 4 项无失败。

追踪需求：R-01、R-02

依赖任务：task-01

修改范围：firmware/src/core/scheduled_task_service.*；firmware/test/test_scheduled_task/test_main.cpp

公共能力处理：扩展 task-01 调度器，复用固定槽和 elapsedMs，不新建第二种调度机制。

完成标准：实现按注册顺序快照、代次检查、回调副本、派发重入保护；迟到 tick 每任务最多执行一次，取消后重新注册排在末尾。

代码注释要求：解释回调自取消的对象寿命、代次快照及无符号回绕。

自动化验证：覆盖自取消、取消其他任务、重注册同 ID、回调改周期/启停、新任务延后执行、递归 triggerNow/tick 拒绝、millis 回绕及迟到多周期不补跑。

执行命令：`pio test -d firmware -e native -f test_scheduled_task`

人工验证关注点：无单独 UI；长时间运行与日历跳变联动在 task-14/15 验证。

待确认问题：无。

## task-03: 共享请求 ID 生成器

本轮结果（2026-09-05）：已实现 64 位共享生成器（小型 header-only 类）；test_exec_id 1 项覆盖序号与耗尽。调用方迁移留到 task-09。

追踪需求：R-08

依赖任务：无

修改范围：firmware/src/core/exec_id_generator.*；firmware/test/test_exec_id/test_main.cpp

公共能力处理：新建共享生成器；已检索 CodexController，旧模块内序号不能保证跨动作唯一。

完成标准：提供开机级 64 位递增序号和固定 16 位十六进制 ID；耗尽失败且不回绕，通过可控初始序号测试上界。此任务先新增，不迁移调用方。

代码注释要求：解释断线不重置、耗尽保护和不持久化原因。

自动化验证：验证首次 ID、连续唯一、两个模拟调用方交替取号、最大序号后拒绝生成。

执行命令：`pio test -d firmware -e native -f test_exec_id`

人工验证关注点：无单独 UI；双动作真实 ID 在 task-15 日志中确认。

待确认问题：无。

## task-04: 有界 JSONL 发送队列

本轮结果（2026-09-05）：已实现 4 槽队列、分片节奏、过期和取消；test_outgoing_jsonl_queue 2 项无失败。

追踪需求：R-09

依赖任务：无

修改范围：firmware/src/core/outgoing_jsonl_queue.*；firmware/test/test_outgoing_jsonl_queue/test_main.cpp

公共能力处理：新建可测发送队列；复用现有 JSONL 常量和 elapsedMs，当前同步发送缺少队列/取消能力。

完成标准：实现 4 槽 FIFO、4096 bytes 上限、LF 规范化、每轮最多一片、20 bytes/10 ms 间隔、10 秒未开始消息过期、取消未发消息、活动帧完成至 LF、代次重置。提供可注入 sink。

代码注释要求：解释活动半帧不可截断、过期只针对未开始消息、满队列失败语义。

自动化验证：覆盖边界大小、内嵌换行、槽满、连续/迟到 poll、完整消息不交错、取消和过期、中文跨片、时钟回绕、sink 失败处理及连接代次清理。

执行命令：`pio test -d firmware -e native -f test_outgoing_jsonl_queue`

人工验证关注点：无单独 UI；实际 notify 速率和丢包在 task-15 验证。

待确认问题：无。

## task-05: BLE 队列接入与连接代次隔离

本轮结果（2026-09-05）：已接入 BLE 队列及代次清理；test_ble_transport 2 项与既有 JSONL 回归无失败。main 仅补单片发送 poll 和连接变化时清旧 Codex 状态；sendJsonl 薄适配保留到 task-09/13，真实 notify 待人工确认。

追踪需求：R-08、R-09

依赖任务：task-04

修改范围：firmware/src/platform/ble_transport.*；firmware/test/test_ble_transport/test_main.cpp；firmware/src/main.cpp 必要发送适配

公共能力处理：扩展现有 BleTransport，复用 task-04 队列及 JsonlBuffer；不新建 BLE 通道。

完成标准：将业务发送改为有界入队，新增 pollTransmit/cancelPending；移除逐片 delay 循环。连接事件和接收 chunk 带代次，快速断开重连也清理旧半帧、旧队列。保留现有调用方可编译的薄适配并接入发送 poll，后续迁移完删除。

代码注释要求：解释回调与主循环状态归属、代次竞态及单片发送前检查。

自动化验证：通过平台可控连接回调验证快速重连、旧 chunk 过滤、断线取消队列、整条分片和发送失败；检查没有整帧 delay 循环。

执行命令：`pio test -d firmware -e native -f test_ble_transport -f test_outgoing_jsonl_queue -f test_jsonl_buffer`

人工验证关注点：真实蓝牙行为待 task-15；native 不能证明 notify 送达。

待确认问题：无。

## task-06: 授时协议与共享样例

本轮结果（2026-09-05）：已更新双端时间字段校验、常量、共享 fixtures 和协议说明；固件 test_protocol 7 项、桌面协议相关用例无失败。保留旧 hello fixture 验证兼容。

追踪需求：R-05、R-06、R-08

依赖任务：无

修改范围：protocol/README.md、protocol/fixtures/；firmware/src/core/message_codec.*、protocol_constants.*；desktop/src/adv_helper/core/protocol_constants.py、messages.py（如需导出）；双端协议测试

公共能力处理：扩展现有 Codec/常量和 fixtures；复用版本 1 信封，不新增协议栈。

完成标准：增加 system.time.read 常量、请求编码、OK 数据字段与严格整数校验；拒绝非法类型/偏移/缺字段，保留未知字段兼容和旧 hello。平台 time_t 可表示性留给时间服务。更新常量一致性 fixture。

代码注释要求：解释 Unix 毫秒单位、UTC 偏移用途及严格类型检查。

自动化验证：覆盖请求/成功/错误、布尔/小数/字符串/超出 int64/缺字段、未知字段、旧 capability 列表；两端读取相同 fixtures。

执行命令：`pio test -d firmware -e native -f test_protocol`；`(cd desktop && uv run pytest tests/test_messages.py tests/test_protocol_constants.py)`

人工验证关注点：无单独 UI；真实消息往返在 task-15 验证。

待确认问题：无。

## task-07: 桌面默认系统授时能力

本轮结果（2026-09-05）：已默认注册被动时间 handler；test_time_module 15 项，以及禁用 Codex/慢查询并发/串行分片集成用例无失败；桌面全量 88 项无失败。

追踪需求：R-05

依赖任务：task-06

修改范围：desktop/src/adv_helper/application/time_module.py、bootstrap.py；desktop/tests/test_time_module.py、test_desktop_integration.py

公共能力处理：复用 ActionRegistry/MessageRouter、并发处理和整条响应发送锁；新建轻量独立 handler，现有 Codex-only 配置不扩展。

完成标准：一次采样生成 epochMilliseconds 及同一时刻偏移；默认注册并宣告 capability。Codex 禁用或失败不影响授时，异常沿用 ERROR；无桌面周期任务。

代码注释要求：解释单次采样、每次重算时区偏移和不依赖用户 actions 配置。

自动化验证：注入时钟覆盖正负偏移、偏移变化、采样失败；Codex 禁用仍可授时；慢 Codex 与授时并发、响应分片不交错、旧查询不回归。

执行命令：`(cd desktop && uv run pytest tests/test_time_module.py tests/test_desktop_integration.py tests/test_config.py)`

人工验证关注点：桌面启动后 hello 包含系统 capability；真实电脑时区与时间在 task-15 比对。

待确认问题：无。

## task-08: 系统 UTC 服务及平台适配

本轮结果（2026-09-05）：已实现 UTC 服务、平台设置/读取和当地时间转换；test_system_time 2 项含窄 time_t 范围、设置失败、负向校时及转换断言，无失败。设备构建完成；服务尚未接入自动授时控制器，真实时间准确度未测。

追踪需求：R-06

依赖任务：task-06

修改范围：firmware/src/core/system_time_service.*、platform/system_clock.*；firmware/test/test_system_time/test_main.cpp

公共能力处理：新建服务及可替换平台接口；检索未发现日历授时实现，复用已有单调时钟用于成功记录。

完成标准：实现 UTC 读写、成功状态、单调成功时间及偏移；先检查整数/秒/当地时间可表示范围，再设置系统时间，成功后提交元数据；native 使用 fake，禁止设置宿主系统时间。

代码注释要求：解释 time_t 范围校验、负向校时允许、先设置后提交状态、UTC 与偏移分离。

自动化验证：覆盖未授时、毫秒/微秒转换、前后校时、平台上下界、正负偏移、设置失败保留旧状态；可注入窄时间范围以覆盖设备边界，不能只用宿主 64 位范围。

执行命令：`pio test -d firmware -e native -f test_system_time`

人工验证关注点：设备 gettimeofday/settimeofday 和重启行为由构建及 task-15 确认。

待确认问题：无。

## task-09: Codex 控制器迁移与超时恢复

本轮结果（2026-09-05）：已将周期迁入 ScheduledTaskService，使用共享 ID 与真实 execId 入队/取消；删除 RefreshPolicy，补充 210 秒超时与成功缓存年龄保护。test_codex_refresh 6 项无失败；main 仅作该任务必要构造、能力门控及 scheduler.tick 接线。

追踪需求：R-03、R-04、R-08

依赖任务：task-02、task-03、task-05

修改范围：firmware/src/application/codex/codex_controller.*、codex_usage_state.*、refresh_policy.*；firmware/test/test_codex_refresh/test_main.cpp；firmware/src/main.cpp 必要构造适配

公共能力处理：复用调度器/共享 ID/队列/缓存；扩展控制器，删除 RefreshPolicy 及过时前台周期断言。

完成标准：后台请求不依赖 pageActive；自动开关使用任务状态，Enter 成功入队才重设周期；能力门控、210 秒超时释放和 cancelPending。失败保留缓存及成功时间，event/action/execId 三重匹配。同步适配构造调用，保持工程可编译。

代码注释要求：解释提交成功与响应成功的区别、超时余量、缓存时间不因失败更新。

自动化验证：覆盖切页后台刷新、无 capability、暂停仍手动、在途关自动后响应、手动失败不推迟、210 秒边界、迟到/错误 event、不刷新旧缓存年龄；重连启停保留。

执行命令：`pio test -d firmware -e native -f test_codex_refresh -f test_codex_page`

人工验证关注点：页面交互在 task-12/15；本任务先用控制器状态验证。

待确认问题：无。

## task-10: 授时控制器首次同步与小时周期

本轮结果（2026-09-05）：新增 TimeSyncController；首次请求、成功后一小时周期、15 秒超时、三重响应匹配、成功年龄饱和计数均已有逻辑测试。与 task-11 合计 test_time_sync 9 项无失败；未接入设备 main，接线留 task-13。

追踪需求：R-07、R-08

依赖任务：task-02、task-03、task-05、task-06、task-08

修改范围：firmware/src/application/time_sync/time_sync_controller.*；firmware/test/test_time_sync/test_main.cpp

公共能力处理：新建授时控制器，复用任务服务、ID、Codec、发送队列和系统时间服务；不增加第二个长期重试任务。

完成标准：首次支持 capability 时同步，匹配响应且设置成功后重新计时一小时；同动作去重，15 秒超时和取消未发消息；成功年龄饱和计数与 needsSync 锁存。

代码注释要求：解释任务触发起点与成功授时记录分离、持续 tick 饱和计数避免长断线回绕误判。

自动化验证：覆盖首次未支持/支持、双动作共享 ID、成功后一小时、系统设置失败、超时、无匹配响应、单调回绕和日历跳变。

执行命令：`pio test -d firmware -e native -f test_time_sync -f test_system_time`

人工验证关注点：首次授时与小时同步由 task-15 真机确认。

待确认问题：无。

## task-11: 授时重试及重连恢复

本轮结果（2026-09-05）：完成 3 秒间隔最多 2 次重试、入队失败计数、重连恢复、同电脑新鲜截止时间保持、身份变化失效、暂停及超长断线回绕。test_time_sync 对应场景无失败；真机断线/换电脑仍待验收。

追踪需求：R-07、R-08

依赖任务：task-10

修改范围：firmware/src/application/time_sync/time_sync_controller.*；firmware/test/test_time_sync/test_main.cpp

公共能力处理：扩展 task-10 局部状态机，复用当前小时任务及单调计时，无新服务。

完成标准：失败后 3 秒最多再试 2 次；入队失败也计次数，耗尽等小时任务或新会话。覆盖同电脑新鲜重连保持截止时间、到期补同步、切换电脑重新同步、断线取消局部重试、暂停不再重试。

代码注释要求：解释 needsSync 保留、每轮重试计数、同电脑重连不重设小时基准。

自动化验证：覆盖 3 秒边界、最多三次尝试、每帧不重试、队列满、失败后重连、到期断线、长于 millis 回绕的持续 tick、电脑切换、暂停在途成功不重启任务。

执行命令：`pio test -d firmware -e native -f test_time_sync`

人工验证关注点：快速重连和换电脑场景由 task-15 验证。

待确认问题：无。

## task-12: Codex 自动状态页脚与开关

本轮结果（2026-09-05）：Fn+Enter 已切换任务开关并置脏，页脚所有中心状态保留自动状态、实际周期 h/m/s 显示、次要内容按宽度裁剪。test_codex_page 7 项与导航 4 项无失败；设备构建完成，物理按键与实际布局待人工确认。

追踪需求：R-04

依赖任务：task-09

修改范围：firmware/src/application/codex/codex_page.*；firmware/src/main.cpp 页面参数/按键置脏适配；firmware/test/test_codex_page/test_main.cpp、test_codex_refresh/test_main.cpp

公共能力处理：扩展 CodexPage，复用 KeyEvent.fn 和全局导航；不新建输入层。

完成标准：Fn+Enter 切换自动刷新且即时重绘；所有已连接页面状态可见自动状态；实际周期格式化为 h/m/s，次要文字裁剪，中心卡片不占底部。保留断线屏。

代码注释要求：解释自动状态优先布局、开关变化为何独立触发置脏。

自动化验证：验证周期标签、开启/关闭、空/加载/错误/缓存状态的 view、Fn+Enter 不触发手动查询，全局导航不受影响。

执行命令：`pio test -d firmware -e native -f test_codex_page -f test_codex_refresh -f test_navigation`

人工验证关注点：入口 Codex 页面：Enter 查询，Fn+Enter 开关；检查 240×135 屏幕上状态、错误、刷新中和长文本不重叠，切页回返设置保留；最终人工结果归 task-15。

待确认问题：无。

## task-13: 主循环双任务与会话编排

本轮结果（2026-09-05）：已接入系统时间实例、双动作路由和主循环时序，新增 computerId 查询用于身份失效；删除临时 sendJsonl 和独立分钟重绘判断。会话与双任务集成共 6 项用例无失败，旧 JSONL 测试迁入 enqueueRequest。

追踪需求：R-02、R-03、R-07、R-08、R-09

依赖任务：task-07、task-09、task-11、task-12

修改范围：firmware/src/main.cpp、core/connection_session.*；firmware/test/test_connection_session/test_main.cpp

公共能力处理：复用现有会话/路由；main 只接线，已有分钟绘制周期迁入调度任务，不新建业务框架。

完成标准：按授时/Codex/显示顺序注册；hello 首次通知、重复幂等、配置变化仅应用变化、身份改变清空旧在途。接收后刷新 now，控制器短时维护→统一 tick→单片发送→绘制。移除临时发送适配和旧分钟周期判断，保持 capability 门控。

代码注释要求：解释 hello 与 BLE 就绪区别、身份/代次失效、时钟取样顺序和显示任务只置脏。

自动化验证：覆盖重复 hello、身份变化、非法 hello、能力增加/移除、周期变化、用户关闭不被 hello 重开；设备编译仅兜底平台 API/接线，不证明行为。

执行命令：`pio test -d firmware -e native -f test_connection_session -f test_codex_refresh -f test_time_sync`；`pio run -d firmware`

人工验证关注点：真机启动、导航、电量分钟更新及双动作闭环在 task-15。

待确认问题：无。

## task-14: 双任务集成与回归验证

本轮结果（2026-09-05）：新增真实控制器/调度器/队列/路由与 BLE 连接事件的集成夹具，覆盖同周期顺序、共享 ID、慢 Codex、开关/后台刷新、hello 变化、快速重连、旧响应、队列过期和半帧取消、UTC 跳变与回绕。固件全量 54 项、桌面全量 88 项无失败；桌面既有慢查询/授时并发与串行响应测试已满足本轮范围，未重复新增。首次全量发现旧测试调用已移除接口，修正后全量重跑无失败。

追踪需求：R-01～R-09

依赖任务：task-13

修改范围：firmware/test/test_scheduled_integration/test_main.cpp；desktop/tests/test_desktop_integration.py；必要测试夹具

公共能力处理：复用实际 Controller/调度器/队列，假时钟和 sink 仅替代平台，不复制业务逻辑；不引入泛化测试框架。

完成标准：按 main 实际时序驱动 hello/控制器维护/tick/发送/响应/断线，覆盖故障组合；双端现有回归执行完成并记录真实结果。

代码注释要求：解释复现故障的事件次序与期望，避免注释重复断言。

自动化验证：覆盖同时到期授时先入队且 ID 唯一、慢 Codex 不阻塞授时、队列满/过期/半帧取消、快速重连、迟到响应、切页/关闭/手动、时间跳变/回绕、不重复 hello 刷新。失败定位后只修本范围；设计偏离先更新规格。

执行命令：`pio test -d firmware -e native`；`(cd desktop && uv run pytest)`；`pio run -d firmware`；`git diff --check`

人工验证关注点：自动化结果与设备验证分开；不得据此标记真机验收完成。

待确认问题：无。

## task-15: 操作说明与真机验收

本轮结果（2026-09-05）：固件/桌面/协议操作说明已更新，main 增加串口小写 `t` 的按需时间诊断，不新增周期或 BLE 动作。文档部分完成；下表均未执行，待用户反馈后再勾选任务。

| 真机场景 | 结果 |
| --- | --- |
| 首次授时、双动作闭环、分钟更新 | 待验收 |
| Fn+Enter、关闭两个周期、手动/恢复/跨页、各状态页脚 | 待验收 |
| 新鲜/到期/快速重连、切换电脑、旧桌面兼容 | 待验收 |
| 一小时 E0/E1、采样与链路延迟、同步截止时间 | 待测量，未声明精度 |
| 完全断电未授时、自动默认开启、重连恢复 | 待验收 |

测量方法：115200 串口发送 `t`，读取 `synced`、`utc_ms`、`offset_min`、`last_sync_ms` 和 `now_ms`；`synced=0` 时 UTC 无效。首次同步后记录设备与电脑 UTC 毫秒差 E0，在距上次同步接近 3600000 ms、下一次同步前记录 E1，再确认同步时间更新。电脑 UTC 可通过 `uv run python -c 'import time; print(time.time_ns() // 1000000)'` 读取。尽量同时采样，记录设备/电脑版本、配置周期、采样耗时及误差；验收关注 `abs(E1-E0) <= 1000 ms`，先后手工读数不能单独证明亚秒精度。

追踪需求：R-02～R-09

依赖任务：task-14

修改范围：firmware/README.md、desktop/README.md、protocol/README.md；本规格 tasks.md、changelog.md

公共能力处理：复用现有运行/刷写说明和桌面诊断日志；无需新公共服务，不默认另建测试报告。

完成标准：更新后台刷新/快捷键/授时与旧桌面兼容说明；按下面步骤记录自动化与人工结果，人工未执行时保持此任务未勾选，不声称全部完成。

代码注释要求：不涉及生产代码；如需最小时间测量诊断，注释测量用途并限定本任务范围，不新增时钟页面。

自动化验证：检查文档命令/字段与最终实现一致，确认未写入未经测量的精度结论。

执行命令：`git diff --check`

人工验证关注点：连接电脑首次授时；在 Codex 按 Fn+Enter 关闭并等待两个周期，无自动请求但 Enter 有效；恢复后等待完整周期；切到其他页等待一个周期再返回看到新缓存。检查所有页脚状态；断线到期后重连、快速重连、切换电脑、旧桌面缺授时 capability。保持非睡眠运行一小时，记录首次和下次同步前设备/电脑 UTC 偏差及测量方式，偏差变化绝对值≤1 秒；确认小时授时日志、双动作 ID/整帧发送。完全断电后重新开机，确认未授时状态及默认自动启用，再连接恢复。测量读取可使用仅本机串口诊断，不建立新业务协议。

待确认问题：无。

## 阶段完成判定

- task-01～14：记录对应新增逻辑自动化结果和构建局限，不把尚未执行的设备验收描述为完成。
- task-15：用户确认真机结果后勾选；若存在未测项或测量失败，明确列出并保留未完成状态。
- 需求覆盖：R-01→01/02/14；R-02→01/02/13/14/15；R-03→09/13/14/15；R-04→09/12/14/15；R-05→06/07/14/15；R-06→06/08/14/15；R-07→10/11/13/14/15；R-08→03/05/06/09/10/11/13/14/15；R-09→04/05/13/14/15。

## 待确认问题

无代码实现阻塞问题。task-13、14 已完成；task-15 仅剩真机验收，需用户提供实际结果后更新完成状态。
