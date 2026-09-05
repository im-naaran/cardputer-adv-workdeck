> 状态：已确认

# ADV 模块运行时配置：任务拆解

## 提交前人工检查反馈（2026-09-05）

用户反馈已按本地文档的相应步骤完成 ADV 等检查，并决定不提交本地准备材料和独立验收清单。正式需求、操作及验收关注点以本规格和 README 为准。该反馈未附逐项结果或时间测量数值，因此不补录具体精度或故障注入结论；下方原执行记录和未勾选验收项保留其证据边界。一期前台刷新策略已由后续后台调度及设备本地配置规格替代。

依据已确认 requirements.md 与 design.md。用户已确认清单，现已完成 task-01～07 的实现与自动化；task-08 保留真机验收。

## 任务清单

- [x] task-01: Codex 配置类型、JSON 校验与默认文件
- [x] task-02: LittleFS 有界读取与安全文件替换
- [x] task-03: Codex 配置服务与调度即时应用
- [x] task-04: 双端移除 hello 周期控制，迁移桌面配置
- [x] task-05: 启动加载、串口配置入口与页面接线
- [x] task-06: 双端集成回归与固件/文件系统镜像构建
- [x] task-07: 更新使用与维护文档
- [ ] task-08: 真机配置、持久化与即时生效验收

## 执行约束

- 按编号串行执行，避免控制器、main、共享 fixtures 交叉修改；依赖只指向较小编号。
- 每项完成更新状态和 changelog。实现与自动化结果单独记录，真机结果不得以构建代替。
- 不升级依赖、不修改分区布局、不暂存或提交用户文件。不自动运行 upload、uploadfs 或 erase；设备验收使用单独记录的操作步骤。
- 默认值集中在 CodexConfig，实体样例用一致性断言保护；避免新增配置缓存、长期轮询和通用框架。
- 命令以下均以项目根目录为起点；桌面使用 Python 模块入口，避免旧 pytest 脚本路径问题。

## task-01: Codex 配置类型、JSON 校验与默认文件

本轮结果（2026-09-05）：配置严格解析、512-byte 限制、尾部校验、默认镜像样例已实现；最终 test_codex_config 4 项无失败。采用轻量读取器避免引入标准 iostream/locale。

追踪需求：R-01、R-04

依赖任务：无

修改范围：`firmware/src/application/codex/codex_config.*`（本任务只实现类型与编解码）、`firmware/data/config/codex.json`、`firmware/test/test_codex_config/test_main.cpp`。

公共能力处理：复用 ArduinoJson；未发现已有模块文件配置类型，新增最小 CodexConfig，不复用 BLE 消息信封。

完成标准：默认 300 秒、范围 60～3600 秒；严格对象/整数/字段校验，最多 512 bytes，拒绝未知字段和尾部非空白内容；提供规范化序列化。默认文件与默认值一致，错误不产生可应用的部分配置。

代码注释要求：解释严格整数检查、尾部数据拒绝和样例与默认值的一致性；不复述 JSON 字段赋值。

自动化验证：覆盖 60/300/3600、上下界外、布尔/小数/字符串、缺字段/未知字段、非对象/损坏 JSON、超长、合法尾部空白及非法尾部内容、序列化往返和镜像样例。

执行命令：`pio test -d firmware -e native -f test_codex_config`。

人工验证关注点：无 UI；样例在设备中的读取留 task-08。

待确认问题：无。

## task-02: LittleFS 有界读取与安全文件替换

本轮结果（2026-09-05）：LittleFS 原分区挂载、有界 stdio/VFS 读取、临时写入核对及 rename 替换已实现；真实临时目录用例注入部分写入/内容损坏/rename 失败，旧文件保留。缺失、目录、超长与未挂载有断言；底层 LittleFS 断电和真实短读尚未实测，归设备边界。buildfs 已生成 /config/codex.json，未上传。

追踪需求：R-01、R-04

依赖任务：task-01

修改范围：`firmware/src/platform/config_file_store.*`、`firmware/platformio.ini`、`firmware/test/test_config_file_store/test_main.cpp` 及必要存储测试替身。

公共能力处理：复用框架 LittleFS 和既有 platform/native 隔离方式；新增有界读取/安全替换接口，不引入第三方依赖。平台实现不认识 Codex 字段，临时内容至少须与预期已校验 JSON 字节一致。

完成标准：核对实际 default_8MB 分区后设置 LittleFS 镜像类型，挂载 `begin(false)`；区分未挂载/不存在/读取错误。限制读取长度，拒绝目录和短读。显式保存时可创建目录，临时文件写入、关闭、重读核对后 rename 替换，禁止先删除正式文件；正常读取无写操作。

代码注释要求：说明挂载失败不格式化、flush 不能证明成功、临时文件不作为有效配置，以及 rename 失败保留旧文件的边界。

自动化验证：对实际读写流程的可控文件操作注入缺失、目录、短读、超长、部分写入、临时重读失败、rename 失败；验证正式文件保留、读取不写盘和临时文件不被加载。不能只让 fake 模拟一个“成功保存”并据此声称平台实现已验证；底层 LittleFS 实际行为记录为设备验收项。

执行命令：`pio test -d firmware -e native -f test_config_file_store`；`pio run -d firmware`；`pio run -d firmware -t buildfs`。构建仅验证平台接口和镜像生成，不上传。

人工验证关注点：挂载、实际替换和文件系统部署在 task-08；实现前核验底层 rename 覆盖行为，发现不符先更新设计。

待确认问题：无。

## task-03: Codex 配置服务与调度即时应用

本轮结果（2026-09-05）：read/reload/save 及 Controller.applyConfig 已实现；每次读取、同值不写/不延后、失败恢复、暂停、回绕及 I/O 后取时已有断言。串口集成另验证在途响应保留和实际截止时间。

追踪需求：R-02、R-03、R-04

依赖任务：task-01、task-02

修改范围：`firmware/src/application/codex/codex_config.*`、`codex_controller.*`、`firmware/test/test_codex_config/test_main.cpp`、`test_codex_refresh/test_main.cpp`。

公共能力处理：新建设计中的 CodexConfigService，复用存储接口、Controller、ScheduledTaskService.updateInterval/getTask；不增加文件轮询任务。

完成标准：read 每次重新读取；reload/save 成功后应用。合法同值保存跳过写入但重读并应用；缺失/损坏允许显式修复，读取 I/O 错误不覆盖。写后重读失败返回 ReloadFailed，任务不存在等应用失败返回 ApplyFailed，保留旧运行周期。I/O 完成后才采样单调时间；实际周期变化才重设起点，enabled/在途/缓存不受影响。

代码注释要求：解释文件值与任务运行状态区别、相同值不重设起点、保存与应用非跨层原子事务、I/O 后取时。

自动化验证：连续读取看到外部变化、启动默认、运行中失败保留状态、300↔60、相同值不写不延后、关闭时改周期、在途响应正常完成、文件已改但运行未更新的同值保存恢复、写/重读/应用失败、I/O 耗时及 millis 回绕。

执行命令：`pio test -d firmware -e native -f test_codex_config -f test_codex_refresh`。

人工验证关注点：真实文件与页脚反馈在 task-08；本任务用实际 Controller/调度器及假存储验证。

待确认问题：无。

## task-04: 双端移除 hello 周期控制，迁移桌面配置

本轮结果（2026-09-05）：双端周期字段已清理，旧桌面键忽略并提示；RPC 默认统一。删除前对旧 Codec 的无 settings 默认 300 秒断言已运行且无失败；删除后新旧 hello 均覆盖。本轮桌面全量 98 项无失败，未运行旧设备二进制。

追踪需求：R-01、R-05

依赖任务：task-03

修改范围：desktop 的 `config.py`、`bootstrap.py`、`os_adapters/codex_app_server.py`、`config.json` 和相关 tests；firmware 的 `message_codec.*`、`connection_session.*`、`codex_controller.*`、`codex_page.*`、`main.cpp` 必要签名适配及相关 tests；`protocol/fixtures/`。

公共能力处理：复用原配置校验、hello 协议及测试；删除周期链路，不新增兼容消息类型。RPC 默认常量归 App Server 适配模块，其他调用方复用。

完成标准：desktop 不保留/下发刷新周期，旧 refreshIntervalSeconds 仅产生废弃提示，其他未知键仍拒绝；RPC 超时保留配置和统一 15 秒默认。固件忽略旧 settings，删除 Message/Session 周期字段，onSessionReady 只处理会话与能力；页面必须接收真实任务快照，不再默认写死周期。

代码注释要求：解释旧键仅兼容忽略、hello 不覆盖本地配置；避免保留失效的“电脑配置周期”注释。

自动化验证：先在旧固件解析实现上运行无 settings hello 的默认 300 秒兼容断言并记录，再移除字段；最终新旧 hello 均可建连且不能覆盖本地周期。覆盖旧配置任意废弃值只告警、其他未知键拒绝、无周期配置时不再警告缺失、RPC 超时范围/默认与实际超时、自动关闭不被 hello 恢复。同步适配所有控制器/页面调用方，不能保留仅供旧测试的生产接口。

执行命令：`pio test -d firmware -e native -f test_protocol -f test_connection_session -f test_codex_refresh -f test_codex_page -f test_scheduled_integration`；`(cd desktop && uv run --locked python -m pytest tests/test_config.py tests/test_messages.py tests/test_codex_app_server.py tests/test_desktop_integration.py)`。

人工验证关注点：新桌面与新设备主流程及旧桌面 hello 在 task-08；旧二进制未实际测试时注明兼容证据仅来自旧源码断言。

待确认问题：无。

## task-05: 启动加载、串口配置入口与页面接线

本轮结果（2026-09-05）：启动挂载/加载、三个串口命令、行缓冲/每轮字节预算、t 兼容、置脏与 I/O 后主循环取时已接入；test_config_commands 1 项复合场景无失败。

追踪需求：R-01、R-02、R-03、R-04、R-06

依赖任务：task-04

修改范围：`firmware/src/main.cpp`、`firmware/src/platform/` 下最小串口行缓冲/固定命令解析文件（如需）、`firmware/test/test_config_commands/test_main.cpp`。

公共能力处理：复用 Serial、现有 t 诊断和 redrawRequested；固定命令调用 CodexConfigService，不另建配置逻辑或通用命令注册框架。

完成标准：挂载后按授时/Codex/显示顺序注册，再加载配置；错误保留默认并诊断。接入 read/reload/save 三条命令，read 同时报告文件值与实际周期，changed 置脏；配置处理在调度前，I/O 后刷新 now。保留 t，行缓冲上限 576 bytes、每轮至多 64 bytes，LF/CRLF，超长行丢弃到换行，不阻塞等待完整输入。

代码注释要求：解释串口预算、超长行不能执行截断指令、t 与行命令共存、应用与主循环取时顺序。

自动化验证：分片输入/多条命令/CRLF/未知命令/超长及后续恢复、带 JSON 的 save、t 兼容、无新数据时不重复执行、单轮字节预算；实际服务与控制器断言保存后下一次调度判断已使用新周期。

执行命令：`pio test -d firmware -e native -f test_config_commands -f test_codex_config`；`pio run -d firmware`。

人工验证关注点：串口发送三条命令并核对输出；Codex 页脚立即变化，连接、导航与 t 不受影响，错误状态可读。最终记录归 task-08。

待确认问题：无。

## task-06: 双端集成回归与固件/文件系统镜像构建

本轮结果（2026-09-05）：最终固件全量 native 62 项、桌面全量 98 项均无失败；双任务集成新增本地配置与旧 hello 共存场景。固件目标构建完成（Flash 1540089 bytes / RAM 50540 bytes），LittleFS 镜像 1572864 bytes，包含 /config/codex.json；未 upload/uploadfs。

追踪需求：R-01～R-05

依赖任务：task-05

修改范围：`firmware/test/test_scheduled_integration/test_main.cpp`、必要配置集成夹具、`desktop/tests/test_desktop_integration.py` 及必要修复。

公共能力处理：复用真实配置服务、控制器、调度器、路由与发送队列，fake 仅代替时钟/存储/硬件；不复制业务算法。

完成标准：按 main 时序覆盖加载、hello、串口保存、调度及响应；新配置生效与授时/重连/后台刷新并存。完成双端全量回归、设备和文件系统镜像构建，记录实际结果及失败修复。

代码注释要求：解释跨模块事件次序和故障注入目的；测试不可只断言内部方法被调用。

自动化验证：周期缩短/延长、反复同值不饿死任务、自动关闭/在途/切页、重连不覆盖、读取/写入失败、I/O 后计时和回绕、首次无文件默认运行。校验 data 镜像路径、默认样例以及旧桌面 RPC 行为。

执行命令：`pio test -d firmware -e native`；`(cd desktop && uv run --locked python -m pytest)`；`pio run -d firmware`；`pio run -d firmware -t buildfs`；`git diff --check`。

人工验证关注点：不能据此声称真实 Flash 原子替换、断电、刷写保留或 BLE 响应性已确认。

待确认问题：无。

## task-07: 更新使用与维护文档

本轮结果（2026-09-05）：根/三子目录 README 已更新，提供本地配置验收清单（不随仓库提交，步骤见 task-08）；链接、协议 JSON 与默认样例检查无报错，git diff --check 无报错。

追踪需求：R-06

依赖任务：task-06

修改范围：根 README、desktop/firmware/protocol README、本规格 tasks/changelog。

公共能力处理：沿用已确定的 README 分工，根目录讲操作，子目录讲代码；不重复另建完整测试报告。

完成标准：根 README 写首次 uploadfs、三条串口命令与立即生效语义，明确 uploadfs 覆盖整个文件系统；firmware 说明读/写/应用及未来 settings 入口；desktop/protocol 移除周期下发说明并解释废弃配置迁移。真机步骤与用户反馈保留在本规格 task-08，自动化结果单独记录。

代码注释要求：不涉及生产代码；文档准确区分持久文件和当前运行周期，以及保存失败后可能已落盘的情形。

自动化验证：核对命令、字段、路径和默认值与源码一致，检查本地 Markdown 链接与 JSON 样例，不写入未经测量的设备结论。

执行命令：`git diff --check`，外加链接/样例静态检查。

人工验证关注点：文档应能引导首次部署和单文件日常修改，不将 uploadfs 当作普通保存命令。

待确认问题：无。

## task-08: 真机配置、持久化与即时生效验收

本轮结果（2026-09-05）：未执行真机刷写、持久化/Flash 异常/屏幕/BLE 响应性验收；保持未勾选。

追踪需求：R-01～R-06

依赖任务：task-07

修改范围：本规格 tasks/changelog 中的验收记录。

公共能力处理：复用串口命令、桌面日志和现有设备页面；不新增业务能力。

完成标准：用户确认下列真机结果后勾选；无法执行或尚未反馈时保持未勾选，明确已完成的代码与未完成的设备验收。

代码注释要求：不涉及生产代码。

自动化验证：汇总 task-06 记录即可，不为文档状态重复构建；硬件结果不能由 native 替代。

执行命令：本任务不自动运行设备写入命令。由用户按 task-07 文档选择串口设备，完成首次文件系统部署和固件刷写，记录操作与结果。

人工验证关注点：

1. 首次部署后 read 为 300；save 60 后无需重连/切页即可看到周期变化，按新周期刷新；改回 300 同样正确。
2. 重复保存同值仍按原截止时间刷新；关闭自动时保存不开启任务，恢复后等新周期；在途请求能完成。
3. 非法输入和超长命令不破坏有效配置，后续正常命令可恢复；read 与 reload 区别符合说明。
4. 重启保留文件值；仅刷应用且分区不变时仍保留。记录实际固件/框架、刷写命令及文件值；不执行无关 erase。
5. 未挂载或文件损坏时默认运行且不自动格式化；可控测试环境下核对保存替换和异常恢复，实际断电行为未测则如实注明。
6. 配置读写期间导航、页面和双动作 BLE 仍可响应；记录明显停顿及文件 I/O 耗时。

待确认问题：无。

## 完成判定

task-01～07 按实现和自动化证据勾选；task-08 单独等待真机确认。所有任务依赖无环、全部需求均已覆盖，不把设备验收默认为已完成。

## 待确认问题

无实现阻塞问题。task-01～07 已完成；task-08 待用户真机结果。
