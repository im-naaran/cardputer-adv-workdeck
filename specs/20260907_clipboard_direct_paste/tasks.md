> 状态：已确认

# 任务拆解

日期：2026-09-07。需求与设计已确认；任务拆解已确认；task-01～10 已完成；2026-09-09 继续完成 UI、自动化回归与文档，task-11 待用户提供真实验收结果。

## 执行约定

- 按编号及依赖实施；本次不安排多代理并行。若用户限定 task 范围，只完成指定范围。
- 每项完成后记录实际代码范围、自动化结果及未完成的人工检查，不将构建或 fake 测试视为真实粘贴验收。
- 文档中的测试名称为计划落点；旧脚本测试随核心重命名迁移，保留原回归场景。
- 原子回滚边界为对应任务的源码与测试；已有后续依赖时按逆依赖顺序回滚。协议 v2 两端迁移期间不部署混合版本，task-01～08 全部完成前不宣布功能可交付。
- 不安装或升级依赖、不修改锁文件；自动化优先使用现有环境。真实剪贴板、焦点输入和刷写由人工验收阶段处理。
- 每项“待确认问题：无”表示没有额外问题，不表示任务清单已获批准。

## 任务清单

- [x] task-01: [协议] 明确 v2 消息与共享 fixtures
- [x] task-02: [电脑配置] 支持剪贴板配置和通用动作契约
- [x] task-03: [进程能力] 复用可取消的子进程创建与回收
- [x] task-04: [macOS] 实现纯文本写入及直接粘贴适配器
- [x] task-05: [电脑核心] 统一目录、快捷键、执行槽及 hello 装配
- [x] task-06: [固件协议] 实现 v2 解码及动作类型能力
- [x] task-07: [固件核心] 统一双目录状态和全局执行控制
- [x] task-08: [固件 UI] 共用列表并接入主循环
- [x] task-09: [回归] 完成跨模块自动化及目标构建
- [x] task-10: [交付文档] 提供配置、升级与人工验收步骤
- [ ] task-11: [人工验收] 确认 macOS 直接粘贴和 ADV 交互

## task-01: [协议] 明确 v2 消息与共享 fixtures

- 追踪需求：R-01、R-03、R-04、R-07、R-08。
- 依赖任务：无。
- 修改范围：`protocol/README.md`、`protocol/fixtures/`，新增或更新协议 fixture 契约检查。
- 公共能力处理：扩展现有 JSONL/hello/动作消息契约；保留封装和 GATT，定义 actions.list 的 type、actions.execute、混合快捷响应和 supportedActionTypes。
- 代码注释要求：fixture 检查说明错误样本的预期拒绝原因，避免将非法 fixture 当成合法通用样本。
- 完成标准：覆盖脚本/剪贴板两类成功、空目录、分页、部分失败、未知结果、无目标、BUSY、版本不匹配；响应字段和 null 语义与 design.md 一致；不含配置正文。旧版本拒绝样本明确命名。此任务只固定契约，运行时迁移由 task-02/05/06 完成。
- 自动化验证：JSON 语法及独立 fixture 断言，核对 4096 字节消息上限和分页一致性；已有消费者尚未迁移导致的预期失败须单独记录，不为临时通过削弱验证。
- 人工验证关注点：只审核协议样本与两端配套升级说明，无设备 UI 操作。
- 待确认问题：无。

### task-01 执行记录（2026-09-07）

- 已补充 protocol/README.md 的 v2 契约，新增 `protocol/fixtures/v2/` 共 27 个样本与独立契约检查。
- 根目录 v1 fixtures 和两端运行时版本保持原状；分目录保留现有回归基线，task-05/06 再配套切换，未提前部署 v2。
- v2 样本包含双类型分页/快捷执行、部分失败、未知结果、BUSY、无匹配、非法消息及结构合法但版本拒绝的 hello。
- 自动化：本轮桌面完整测试集 289 passed；fixture 检查包含在其中。人工关注：后续两端升级/连接验收尚未开展。

## task-02: [电脑配置] 支持剪贴板配置和通用动作契约

- 追踪需求：R-02、R-03、R-04、R-07。
- 依赖任务：task-01。
- 修改范围：`desktop/src/adv_helper/config.py`、`core/script_contract.py` 向 `action_contract.py` 迁移、`core/messages.py`、相关配置和协议单元测试；更新直接引用方使导入可用。
- 公共能力处理：扩展 ActionConfig 和既有 UTF-8/名称/key 校验；不新增平行配置加载器。
- 代码注释要求：解释正文不 trim、先校验再保存、类型成功条件与 null 未确认语义。
- 完成标准：clipboard ID/name/key/params 按设计校验；正文原样保留且不进入错误日志；允许纯空白，拒绝空串/NUL/非法编码/超长正文。通用契约识别双类型目录与结果，保留脚本退出码校验。旧脚本模块迁移前可保留最小内部兼容调用，但 task-05 必须清除已被替代的兼容层。
- 自动化验证：配置和动作契约单测；覆盖 8192 字节与越界、中文/emoji/CRLF/Tab/首尾空格、孤立代理项、重复 ID、混合类型 key、未知参数；合法/非法共享 fixture 均有预期结果。
- 人工验证关注点：审核配置错误仅包含字段或错误类别；不执行配置正文。
- 待确认问题：无。

### task-02 执行记录（2026-09-07）

- 新增 action_contract.py，统一基础元数据校验并实现严格 v2 请求、目录、执行三态和类型能力校验；messages.py 可校验 typed v2 响应。
- config.py 接受 clipboard 配置，保留全空白、换行与 Unicode，拒绝非法/超长正文；未知配置字段不输出原始键，避免将误填正文写入诊断。
- script_contract.py 暂保留 v1 响应校验，复用通用基础函数；这是 task-05 runtime 迁移前的过渡入口，不代表混合版本兼容承诺。剪贴板配置当前仅可加载，尚不能执行。
- 自动化：配置、契约及既有桌面集成纳入本轮 289 passed。人工关注：真实配置目录展示和粘贴留待后续任务。

## task-03: [进程能力] 复用可取消的子进程创建与回收

- 追踪需求：R-05、R-06、R-07、R-08。
- 依赖任务：无。
- 修改范围：`desktop/src/adv_helper/os_adapters/script_runner.py`、同目录最小子进程辅助实现、`desktop/tests/test_script_runner.py` 及新增辅助能力测试。
- 公共能力处理：从现有脚本执行器抽取创建与取消回收能力，补充 stdin 与有限状态输出通道；不扩展为命令框架。
- 代码注释要求：解释创建中取消、重复取消时继续回收、进程组清理和清理未确认后的禁用状态。
- 完成标准：ScriptRunner 保持 `/bin/sh -c`、cwd、环境、DEVNULL 和退出码语义；粘贴调用可传固定 argv 和 stdin 字节；创建/通信/等待均可取消，TERM/KILL 有界，清理未确认禁止执行器继续发起 OS 操作。
- 自动化验证：现有脚本执行器回归；fake 创建延迟、stdin 阻塞、超时、重复取消、清理异常；用无副作用测试子进程验证回收，无真实剪贴板操作。
- 人工验证关注点：无 UI；审查共享辅助能力未改变脚本业务行为。
- 待确认问题：无。

### task-03 执行记录（2026-09-07）

- 新增 ProcessRunner，复用子进程生命周期管理；ShellScriptRunner 保留 shell/cwd/DEVNULL/退出码语义。
- 新能力包括原样 stdin、受限状态输出、覆盖创建与 I/O 的超时、重复取消回收、清理未确认后的执行器禁用。
- 自动化覆盖真实无副作用子进程的 stdin/输出溢出/背压超时回收，以及 fake 的创建延迟、写入中取消、重复取消、等待异常与清理失败；原脚本执行器场景保留。
- 桌面目录执行 `.venv/bin/python -m pytest -q`，结果为 **289 passed in 1.95s**；使用既有环境，未安装依赖。此前定向集为 199 passed，随后补充边界用例并完成全量回归。`git diff --check` 无格式错误。
- 未运行固件构建或刷写（本批无固件变更），未调用真实剪贴板或模拟按键；人工验收尚未开始。

## task-04: [macOS] 实现纯文本写入及直接粘贴适配器

- 追踪需求：R-01、R-05、R-06、R-07、R-08。
- 依赖任务：task-03。
- 修改范围：`desktop/src/adv_helper/os_adapters/paste_runner.py`、必要的 macOS 固定程序资源、新增粘贴适配器测试。
- 公共能力处理：新增 PasteRunner/PasteResult 与平台工厂；复用 task-03 子进程能力。Darwin 选择 macOS 实现，其他系统返回不可用，保留后续 Windows/Linux 接口。
- 代码注释要求：解释纯文本类型、正文只走 stdin、写入成功才粘贴、两阶段状态、50ms/150ms 等待用途及非目标消费保证。
- 完成标准：按设计使用 JXA/AppKit 显式写入纯文本、固定 System Events 粘贴；总预算 10 秒，不附加 Enter、不 activate、不恢复旧内容。权限错误只按明确数值类别映射，输出不含正文或原始 stderr；取消不启动下一阶段。使用资源文件时可从安装包读取。
- 自动化验证：fake 子进程检查固定 argv、UTF-8 stdin 原样、写入失败零粘贴、部分失败/超时/取消、不支持平台及清理失败禁用；固定程序语法可用只编译方式检查，不执行系统副作用。人工真实调用留给 task-11。
- 人工验证关注点：task-11 在文本编辑器、浏览器和 IDE 验证实际纯文本、焦点和权限；本任务只登记待验收。
- 待确认问题：无。

### task-04 执行记录（2026-09-07）

- 已实现 PasteRunner/PasteResult、MacOSPasteRunner 和不可用平台回退；Windows/Linux 预留同一接口，本期未实现。
- 固定 JXA 程序通过 stdin 获取 UTF-8 正文并用 AppKit 显式写纯文本；固定 AppleScript 调用 System Events 发送 Command+V。程序常量随 Python 源码交付，无新增依赖或外部资源路径。
- 10 秒总预算覆盖写入、50ms 等待、粘贴和150ms等待；取消沿用共享子进程回收，清理失败后不可再次执行。部分失败、权限拒绝和结果未知均保留明确状态，不回传正文/原始 stderr，不恢复旧剪贴板。
- macOS SDK 头文件只读确认权限错误号 -1743 / -25211；固定程序经 `/usr/bin/osacompile` 只编译检查，JavaScript/AppleScript 退出码均为 0，未执行程序。
- 自动化覆盖正文原样 stdin、两阶段顺序、失败不发键、部分失败、超时、阶段中取消、禁用执行器和平台选择；纳入本轮完整测试 328 passed。
- 人工关注：真实剪贴板、权限宿主、前台焦点、中文/多行插入尚未验收；未发送真实按键或修改当前剪贴板。

## task-05: [电脑核心] 统一目录、快捷键、执行槽及 hello 装配

- 追踪需求：R-01～R-08。
- 依赖任务：task-01、task-02、task-04。
- 修改范围：`application/script_module.py` 演进为 `actions_module.py`、`bootstrap.py`、`core/protocol_constants.py`、相关消息/集成/会话测试。
- 公共能力处理：扩展现有动作模块，复用 registry、session、发送锁；统一注册三个动作，不创建 ClipboardModule 或第二套快捷键索引。
- 代码注释要求：解释全局首项后分页、不可用首项不回退、第一次 await 前占槽、清理完成后释放和 execId 所有权覆盖响应发送。
- 完成标准：script/clipboard 统一目录和执行，按类型分派执行器；目录不占 busy，脚本与粘贴交叉忙时拒绝；支持 PasteRunner 注入。hello 升到 v2，按执行器能力生成 supportedActionTypes；粘贴初始化失败不拖垮脚本。删除已被替代的 script 专用业务核心及临时兼容入口。
- 自动化验证：混合配置超过两页、跨类型重复键、无缓存快捷执行、脚本↔粘贴 BUSY、取消与异常恢复、发送阻塞时重复 execId 不重复执行、断线无重放、Codex/授时并发；全程使用 fake 粘贴执行器。同步迁移 hello 预期。
- 人工验证关注点：待 task-11 核对脚本与剪贴板任意页快捷触发；本任务不向当前焦点发送按键。
- 待确认问题：无。

### task-05 执行记录（2026-09-07）

- ScriptModule 已替换为 ActionsModule，脚本与剪贴板共用有序目录、快捷键首项归属和 busy 槽；先全局匹配再类型分页，平台不可用首项不回退。保留 ScriptRunner 作为具体执行器。
- 装配支持注入 PasteRunner；粘贴初始化失败只关闭粘贴能力。hello 使用 v2 与 supportedActionTypes；动作统一为 actions.list/actions.execute/actions.shortcut.execute。
- 删除 script_contract.py 的 v1 临时校验入口，协议与消息使用通用类型契约；桌面旧脚本测试迁移到 v2 并保留原场景，根 v1 fixtures 留给尚未迁移的固件。
- 新增混合类型超过两页、两种首项顺序、交叉 BUSY、异常日志不含正文、执行器禁用、断线重连无重放、粘贴响应发送阻塞期间 execId 所有权等回归。
- 在 desktop 执行 `.venv/bin/python -m pytest -q`，结果 **328 passed in 1.97s**；`git diff --check` 无格式错误。未安装依赖或修改锁文件，未构建/刷写固件。
- 当前电脑端协议为 v2，固件仍为 v1，不能配套建立业务会话；task-06 及后续固件任务尚未开始。真实设备 UI 和全局快捷粘贴留待后续人工验收。

## task-06: [固件协议] 实现 v2 解码及动作类型能力

- 追踪需求：R-01、R-03、R-04、R-07、R-08。
- 依赖任务：task-01。
- 修改范围：`firmware/src/core/protocol_constants.h`、`message_codec.*`、`connection_session.*`、协议/连接测试；旧控制器必要的编译兼容调用适配。
- 公共能力处理：扩展 MessageCodec 与 ConnectionSession；ScriptEntry 演进为含 type 的 ActionEntry，保留长度感知提取及消息大小约束。
- 代码注释要求：解释 v1 拒绝、类型/ID 对应、三态字段与不同类型成功条件。
- 完成标准：正确编码新请求、解码双类型目录和结果，校验 supportedActionTypes；拒绝不一致类型、非法元数据、伪成功和 v1 hello。旧调用最小适配仅保证过渡编译，task-07 清除脚本专用编码接口。
- 自动化验证：native 协议与连接测试消费共享 fixtures；覆盖 null/false/true、NUL、错误字段类型、错误 ID 前缀、分页越界、超长 JSON、未知类型和版本不匹配。
- 人工验证关注点：无新增 UI；配套升级与不匹配连接画面留给 task-11。
- 待确认问题：无。

### task-06 执行记录（2026-09-07）

- 固件协议升级到 v2；MessageCodec 使用 ActionEntry、ActionType 和 NullableBool，编码 typed actions.list / actions.execute，解码双类型目录、执行结果与 supportedActionTypes。删除脚本专用编码方法和协议常量。
- 校验类型与 ID 前缀、元数据 UTF-8/长度/NUL、分页关系、重复 ID、nullable 字段、失败原因和类型成功条件；拒绝 v1 hello、未知/重复能力类型、伪成功及正文泄漏字段。ConnectionSession 保存并在断线/身份变更时清空类型能力。
- native 协议测试直接消费 v2 共享 fixtures，保留 Codex、授时及既有脚本边界场景；连接测试覆盖类型能力替换和清理。结果纳入本轮 119 个 native 用例。
- 人工关注：两端版本不匹配的实际连接画面仍待 task-11；本轮未刷写设备。

## task-07: [固件核心] 统一双目录状态和全局执行控制

- 追踪需求：R-03、R-04、R-06、R-07、R-08。
- 依赖任务：task-06。
- 修改范围：`firmware/src/application/scripts/scripts_controller.*` 演进为 `application/actions/actions_controller.*`，对应 controller/native 集成测试。
- 公共能力处理：扩展现有分页、超时与执行关联；两个目录状态和一个执行状态共用控制器，不为剪贴板复制实现。
- 代码注释要求：解释按类型定位目录、共享执行所有权、超时先于响应处理、缓存失效与 Enter 重读不执行。
- 完成标准：首次进入按类型加载；每类缓存一页、选择独立，两个目录请求可并存；全局快捷键不依赖目录。15 秒目录/45 秒动作超时、3 秒终态反馈按设计实现。忙时不替换在途 ID；断线一次清空全部状态；保留必要过渡装配至 task-08，最终只用新核心。
- 自动化验证：native 控制器测试覆盖跨页/切页/响应逆序、错误 type/目标 ID、迟到响应、失败 Enter、快捷键双类型结果、全局 busy、millis 回绕及断线重连不重放。
- 人工验证关注点：task-11 验证双页选择保持、切页时全局反馈与快速按键行为。
- 待确认问题：无。

### task-07 执行记录（2026-09-07）

- 新增 application/actions/actions_controller.*，从旧脚本控制器迁移唯一业务核心。两类目录分别按首次进入加载、缓存一页并维护选择/请求关联；共用动作执行槽及全局反馈。不同类型目录可同时在途、逆序返回，全局快捷键不依赖目录。
- 保留 15 秒目录/45 秒动作超时和 3 秒终态反馈；响应处理先检查超时，忙时不替换请求，错误目录 Enter 只重读，部分粘贴失败显示“粘贴失败，文本已复制”，断线一次清理全部目录和执行状态，不重放。
- ScriptsController 仅保留向新核心委托的临时 UI/装配适配层，无独立目录或执行逻辑；旧 ScriptsPage 和 main 只作编译与 v2 能力/动作接线所需的最小修改。兼容入口仍保持旧脚本预加载时机，task-08 将删除此适配层并把两个页面统一接入首次进入加载；剪贴板占位页本轮未替换。
- 新增双目录逆序/独立选择/跨页、错误 type/目标 ID、共享 busy、部分失败、快捷键双类型、millis 回绕、迟到响应和断线无重放测试；既有脚本控制器、页面、输入集成场景保留。
- 自动化命令：`~/.platformio/penv/bin/pio test -d firmware -e native`；结果为 **119 test cases: 119 succeeded in 00:00:38.911**（29 个测试套件）。首次沙箱调用被 PlatformIO 缓存锁权限阻止，使用已授权命令在沙箱外重跑；未安装依赖或修改锁文件。`git diff --check` 无格式错误。
- 本轮未执行目标构建、刷写、真实 BLE、中文像素或 macOS 剪贴板/焦点验收；对应后续 task-08～11，不能以 native 结果替代。

## task-08: [固件 UI] 共用列表并接入主循环

- 追踪需求：R-01、R-03、R-04、R-06、R-07、R-08。
- 依赖任务：task-05、task-07。
- 修改范围：`application/scripts/scripts_page.*` 演进为 `application/actions/action_list_page.*`、`firmware/src/main.cpp`、必要的 placeholder 接入清理、页面/输入/集成测试。
- 公共能力处理：复用脚本四行布局、AppShell 顶栏/反馈、InputRouter、发送队列；两个列表实例仅保留独立滚动位置和类型文案。
- 代码注释要求：解释首次进入加载、全局反馈不绑定页面、clearModuleSession 覆盖两页，避免重复说明绘图语句。
- 完成标准：Fn+2/Fn+3 分别展示脚本/剪贴板，同一核心处理 Enter/Alt；当前动作页在 hello 到达后也可开始加载。剪贴板占位替换完毕，旧专用控制/编码/页面实现和临时兼容调用清除。设置编辑、方向映射、Fn+Enter、Codex 与授时行为保留。
- 自动化验证：页面 view/native 集成测试覆盖两类文案、四行滚动、裁剪区域、错误重读；已有输入、导航、设置与调度集成回归；目标编译检查装配正确。像素和中文显示不由 view 测试判定。
- 人工验证关注点：Fn+3 → 上下选择 → Enter，检查高亮、中文、计数、“Enter 粘贴”；切回脚本确认独立滚动；离线、空目录、不支持及失败画面分别检查。
- 待确认问题：无。

### task-08 执行记录（2026-09-09）

- 新增 ActionListPage，复用四行列表与 UTF-8 裁剪，按类型展示脚本/剪贴板状态与“Enter 执行/粘贴”；两个实例滚动独立。
- main.cpp 统一 ActionsController，首次进入动作页或当前页收到 hello 时加载对应类型；全局 Alt、Enter、反馈、两类目录超时重绘和断线清理均接入；移除旧 ScriptsController/ScriptsPage 临时实现。
- 迁移保留既有脚本回归场景，新增剪贴板文案/滚动/裁剪与实际 main.cpp 双页集成用例。设置、输入和调度原场景保留。完整 native 121/121，目标构建 SUCCESS（详见 acceptance.md）。
- 中文像素、真机按键和真实粘贴待 task-11；本轮未刷写。

## task-09: [回归] 完成跨模块自动化及目标构建

- 追踪需求：R-01～R-08。
- 依赖任务：task-08。
- 修改范围：必要的缺口回归测试、同规格目录中的验证记录；仅修复本功能引入的缺陷，范围变化先回到对应规格阶段。
- 公共能力处理：复用已有 fake BLE、fake runner、共享 fixtures 和 native 测试设施，不再新增平行测试框架。
- 代码注释要求：新增回归用例明确复现触发条件；不添加复述实现的测试。
- 完成标准：检查所有旧脚本场景已迁移保留、生产中无双核心或失效 scripts.execute 引用；确认日志无正文、两端协议一致、配置版本不变。记录命令、结果、环境阻碍和人工未验收项。
- 自动化验证：在 desktop 运行 `uv run --locked python -m pytest`；项目根运行 `pio test -d firmware -e native` 与 `pio run -d firmware -e m5stack-cardputer-adv`。使用已有环境，不安装/下载依赖；环境不足时记录阻碍，不声称检查完成。补足取消发生于创建/写入/等待/发送各阶段的行为覆盖。
- 人工验证关注点：本任务不刷写、不自动改变当前剪贴板；任务完成仅代表代码与自动化证据齐备。
- 待确认问题：无。

### task-09 执行记录（2026-09-09）

- 补充粘贴前后 settle 等待期间取消用例，结合既有进程创建、stdin 写入、OS 粘贴阶段取消用例覆盖完整生命周期；无真实剪贴板/按键调用。
- 桌面 `UV_OFFLINE=1 uv run --locked --no-sync python -m pytest -q`：330 passed in 2.06s。使用 --no-sync 保证仅使用已有环境，无安装下载。
- 固件 `~/.platformio/penv/bin/pio test -d firmware -e native`：29 套件，121/121，30.520 秒。
- `~/.platformio/penv/bin/pio run -d firmware -e m5stack-cardputer-adv`：SUCCESS，30.879 秒；RAM 74,764/327,680，Flash 2,095,293/3,342,336。
- 首次执行因沙箱无法访问 PlatformIO/uv 缓存而停止，按已有授权在沙箱外重跑。没有安装依赖、修改锁文件或刷写。
- 生产源码已无 ScriptsController/ScriptsPage、旧 application/scripts 引用或 scripts.execute。两端 v2、配置 v1；正常/诊断日志正文保护沿用桌面回归。git diff --check 无格式错误。结果不代表真实设备验收。

## task-10: [交付文档] 提供配置、升级与人工验收步骤

- 追踪需求：R-01～R-08。
- 依赖任务：task-09。
- 修改范围：根 `README.md`、`desktop/README.md`、`firmware/README.md`、`protocol/README.md`、`desktop/config.json`、本规格目录 `acceptance.md`。
- 公共能力处理：沿用现有 README 分层和人工验收格式；根说明保持简洁，故障与权限放子文档。
- 代码注释要求：示例 JSON 不加入非标准注释；正文解释共享 key 和忙碌槽。
- 完成标准：提供一个不带提交换行的剪贴板示例、混合重复键和超过两页的可复制验收配置；明确 v2 两端配套升级、configVersion=1、macOS 首发和 Windows/Linux 仅预留。说明权限宿主、写入后粘贴失败、首次授权重试、终端多行行为及不恢复旧剪贴板。
- 自动化验证：使用正式配置加载器检查示例，确认 key 冲突按预期；检查 Markdown 本地链接和操作入口与最终代码一致。不以真实运行示例验证正文。
- 人工验证关注点：验收表包含入口、步骤、预期、实际结果、设备/系统环境与日期，所有未执行项明确待确认；自动化记录与人工记录分开。
- 待确认问题：无。

### task-10 执行记录（2026-09-09）

- 更新根、桌面、固件及协议 README，清除过渡期 v1 说明，记录配套升级、统一目录/共享槽、macOS 权限与部分失败/超时边界。根说明保留简洁操作入口。
- desktop/config.json 增加无末尾换行的 npm install 文本，Alt+P 触发；保留原脚本配置。acceptance-config.json 提供 17 条剪贴板、两种跨类型重复键顺序和 Unicode/RTF/EPS 文本。
- 使用正式 load_config 与 ActionsModule（fake 执行器，仅请求元数据）检查两个配置：无配置错误、剪贴板页数 8/8/1、全局首项 effectiveKey 符合预期；检查交付 Markdown 本地链接目标均存在。
- 新建 acceptance.md，分开自动化证据与 19 项待执行人工记录，含环境、入口、步骤、预期和实际结果栏。task-11 未勾选，无真实剪贴板/按键副作用。

## task-11: [人工验收] 确认 macOS 直接粘贴和 ADV 交互

- 追踪需求：R-01～R-08。
- 依赖任务：task-10。
- 修改范围：本规格目录 `acceptance.md`、`tasks.md`、`changelog.md` 的真实结果记录；发现缺陷另按对应实现任务修复并补回归。
- 公共能力处理：不涉及新增公共能力，执行已编写的验收步骤。
- 代码注释要求：不涉及代码注释；准确记录预期与实际差异。
- 完成标准：用户确认真实 macOS、BLE、ADV 画面与当前输入框验收结果后才勾选本项。没有设备或人工结果时保持未完成，不以构建代替。
- 自动化验证：沿用 task-09 结果；只在缺陷修复后重跑受影响用例。真实焦点输入不放入常规自动化测试。
- 人工验证关注点：依次检查两端版本、Fn+3 页面和两类独立分页；编辑器/浏览器/IDE 的中文、emoji、多行、Tab、首尾空格及 RTF/EPS 头原文；不抢焦点、不附加 Enter、保留新剪贴板；全局 Alt 首项匹配、忙时拒绝、完成后重复；权限失败/部分失败/超时；断线重连无重放及 Codex/授时/设置回归。授权提示出现后重新聚焦再触发，记录限制。
- 待确认问题：无额外设计问题；执行时需用户提供真实环境和验收结果，未取得前不将人工验收标为完成。

## 阶段确认

任务拆解已确认，task-01～10 已完成。task-11 验收材料已就绪，但真实设备、权限和焦点输入结果未取得，保持未完成；详见 acceptance.md。
