> 状态：已确认

# 第一期整体框架与 Codex 用量查询：任务拆解

## 提交前人工检查反馈（2026-09-05）

用户反馈已按本地文档的相应步骤完成 ADV 等检查，并决定不提交本地准备材料和独立验收清单。正式需求、操作及验收关注点以本规格和 README 为准。该反馈未附逐项结果或时间测量数值，因此不补录具体精度或故障注入结论；下方原执行记录和未勾选验收项保留其证据边界。一期前台刷新策略已由后续后台调度及设备本地配置规格替代。

## 执行原则

- 严格按依赖顺序执行；电脑端和固件端中修改范围不重叠的任务可分别推进，但同一文件范围内串行处理。
- 每个任务只实现其列出的最小能力；快捷脚本、剪贴板和设置只保留占位入口。
- 自动化测试和构建检查由 Codex 执行；真机 UI 效果及电脑端实际 BLE/Codex 查询由用户确认。
- 用户未确认人工验证前，不宣称真机、端到端或生产可用。

## 共享协议

- [x] task-01: [协议] 建立双端共享协议常量与契约样例

  - 追踪需求：R-04、R-08、NFR-01、NFR-04
  - 依赖任务：无
  - 修改范围：`protocol/README.md`、`protocol/fixtures/*.json`
  - 公共能力处理：新建。仓库没有协议资产；固定独立 GATT Service/Characteristic UUID、协议版本、actionId、结果代码及 hello/查询样例，不复用参考项目 UUID。
  - 代码注释要求：协议文档说明电脑端主动 hello 与“Codex 查询被动处理”并不冲突，并说明 JSONL 不允许格式化换行。
  - 完成标准：包含 hello、查询请求、成功、未登录、错误、单窗口、多窗口和未知字段样例；字段与已确认设计一致。
  - 自动化验证：使用 `jq empty protocol/fixtures/*.json` 校验所有 JSON；使用小型脚本检查 UUID/actionId/execId 在 fixtures 中一致，局限是只验证静态契约、不验证双端实现。
  - 人工验证关注点：无需真机；用户可审核字段命名是否易于后续模块扩展。
  - 待确认问题：无

## 电脑端 Python

- [x] task-02: [电脑端基础] 建立 Python 3.12 工程、配置模型与测试环境

  - 追踪需求：R-03、R-05、NFR-01、NFR-03、NFR-04
  - 依赖任务：task-01
  - 修改范围：`desktop/pyproject.toml`、`desktop/.python-version`、`desktop/config.json`、`desktop/src/adv_helper/config.py`、`desktop/tests/test_config.py`、生成的 `desktop/uv.lock`
  - 公共能力处理：迁移并扩展参考项目集中式 dataclass 配置与显式校验模式；保留 Python `>=3.12,<3.14` 和 Bleak 依赖边界，增加 Codex 与协议设置。
  - 代码注释要求：解释刷新间隔为何只同步给 ADV、不在电脑端启动定时器；解释拒绝 Python 3.14 的已知兼容边界。
  - 完成标准：`uv sync` 可创建环境；严格加载 configVersion、Codex 设置和 actions；默认值、范围、禁用动作及可隔离的非法动作符合设计。
  - 自动化验证：`cd desktop && uv run pytest tests/test_config.py`；测试默认 300 秒、范围边界、未知字段、重复 actionId、禁用 action 和非法项隔离。
  - 人工验证关注点：用户后续确认 `uv run adv-workdeck --help` 在实际终端可启动；本任务不要求蓝牙权限。
  - 执行记录：已使用 `uv sync` 创建 Python 3.12.13 环境并生成锁文件；`uv run pytest tests/test_config.py` 共执行 10 个配置用例。按用户补充要求，默认配置文件直接使用 `desktop/config.json`。CLI 实际启动留待 task-07。
  - 待确认问题：无

- [x] task-03: [电脑端核心] 实现 JSONL Codec、消息模型、会话与路由

  - 追踪需求：R-03、R-04、R-08、NFR-01、NFR-02、NFR-04
  - 依赖任务：task-01、task-02
  - 修改范围：`desktop/src/adv_helper/core/{messages,framing,router,session}.py`、`desktop/tests/test_{messages,framing,router,session}.py`
  - 公共能力处理：新建。使用共享 fixtures；从参考项目迁移 notify 队列思路，但将字符串缓冲改为 bytes 缓冲，完整行后再解码 UTF-8。
  - 代码注释要求：解释按 bytes 组装是为了避免中文跨 BLE 分片损坏；解释断线必须清除会话业务数据。
  - 完成标准：支持 4 KiB 上限、拆包/粘包、多行提取、UTF-8 错误、基础字段校验、actionId 路由、execId 关联及断线清理。
  - 自动化验证：`cd desktop && uv run pytest tests/test_messages.py tests/test_framing.py tests/test_router.py tests/test_session.py`；包含中文跨 20 字节边界、溢出、未知 action 和迟到响应用例。
  - 人工验证关注点：无需真机。
  - 执行记录：`uv run pytest tests/test_messages.py tests/test_framing.py tests/test_router.py tests/test_session.py` 共执行 20 个用例，覆盖共享 fixtures、UTF-8 跨片、拆粘包、溢出、超时、未知动作、异常隔离和断线清理。
  - 待确认问题：无

- [x] task-04: [Codex 适配器] 实现 Codex App Server JSONL 客户端

  - 追踪需求：R-06、NFR-01、NFR-02、NFR-04
  - 依赖任务：task-02
  - 修改范围：`desktop/src/adv_helper/os_adapters/codex_app_server.py`、`desktop/tests/fakes/fake_codex_app_server.py`、`desktop/tests/test_codex_app_server.py`
  - 公共能力处理：新建独立 `RateLimitProvider` 实现；复用官方 stdio JSONL 初始化流程和本机 Codex 登录，不读取凭证文件。
  - 代码注释要求：解释惰性启动、一次连接只初始化一次、为何忽略非关联通知，以及子进程退出后不隐藏重试服务查询。
  - 完成标准：支持 initialize/initialized、account/read、account/rateLimits/read、RPC id 关联、超时、进程退出和关闭；将外部结构转换为稳定内部模型。
  - 自动化验证：`cd desktop && uv run pytest tests/test_codex_app_server.py`；由假子进程覆盖已登录、未登录、单桶、多桶、可选字段缺失、畸形响应、超时和退出。
  - 人工验证关注点：本任务不调用真实账号；用户在最终联调任务确认实际 Codex 登录读取。
  - 执行记录：按官方 App Server 协议实现惰性 stdio 客户端；`uv run pytest tests/test_codex_app_server.py` 共执行 8 个假进程用例，未调用真实账号。
  - 待确认问题：无

- [x] task-05: [Codex 业务] 实现被动查询处理器与结果转换

  - 追踪需求：R-05、R-06、R-08、NFR-02、NFR-03、NFR-04
  - 依赖任务：task-03、task-04
  - 修改范围：`desktop/src/adv_helper/application/codex_module.py`、`desktop/src/adv_helper/core/registry.py`、`desktop/tests/test_codex_module.py`
  - 公共能力处理：扩展 task-03 Router 与 Action Registry；查询只依赖 `RateLimitProvider`，不得直接依赖子进程或 BLE。
  - 代码注释要求：解释电脑端不设置周期任务；解释 QueryGate 只是防御意外并发、ADV 才是主调度方。
  - 完成标准：只有收到有效 `codex.usage.read` 才查询；正确回传 execId；标准化多/单额度桶；错误映射为稳定 result.code；并发请求返回 BUSY。
  - 自动化验证：`cd desktop && uv run pytest tests/test_codex_module.py`；显式断言空闲期间 provider 调用次数为 0、每个有效请求至多一次额度读取、百分比边界和错误映射正确。
  - 人工验证关注点：无需真机。
  - 执行记录：`uv run pytest tests/test_codex_module.py` 共执行 9 个用例，覆盖空闲零查询、单请求单读取、execId、异常百分比、BUSY 和稳定错误映射。
  - 待确认问题：无

- [x] task-06: [电脑端 BLE] 实现可重连 Bleak Transport

  - 追踪需求：R-03、R-04、NFR-01、NFR-02、NFR-03
  - 依赖任务：task-02、task-03
  - 修改范围：`desktop/src/adv_helper/platform/ble_transport.py`、`desktop/tests/test_ble_transport.py`
  - 公共能力处理：迁移并模块化参考项目 `ReconnectableBleTransport`、Service UUID 优先扫描、普通扫描回退、连接后延迟、GATT discovery 有限重试、notify 订阅和 macOS 权限错误提示；使用本项目独立 UUID。
  - 代码注释要求：解释 CoreBluetooth UUID 不等于公开 MAC；解释扫描回退、1 秒 discovery 延迟和退避的实际兼容目的。
  - 完成标准：支持扫描、唯一设备选择、可选名称/ID、连接、特征校验、notify bytes 入队、20 字节顺序 write-with-response、断开回调和有上限重连退避。
  - 自动化验证：`cd desktop && uv run pytest tests/test_ble_transport.py`，使用假 Bleak 对象覆盖扫描回退、多设备冲突、特征缺失、分片、notify 和断线；真实 CoreBluetooth 不在自动化范围内。
  - 人工验证关注点：最终由用户在 macOS 授权蓝牙，确认发现和连接 Cardputer-Adv；拒绝权限时应看到明确指引。
  - 执行记录：代码审查修复后 `uv run pytest tests/test_ble_transport.py` 共执行 11 个假 Bleak 用例，补充写失败重连和队列溢出诊断；真实 macOS 权限、发现、连接和重连仍留待用户按 task-14 清单确认。
  - 待确认问题：无

- [x] task-07: [电脑端集成] 装配后台应用、hello 与诊断日志

  - 追踪需求：R-03、R-04、R-05、R-06、NFR-02、NFR-03、NFR-04
  - 依赖任务：task-05、task-06
  - 修改范围：`desktop/src/adv_helper/{main,bootstrap}.py`、`desktop/src/adv_helper/platform/diagnostics.py`、`desktop/tests/test_desktop_integration.py`、`desktop/README.md`
  - 公共能力处理：复用 task-02～06，不新建第二套路由或任务循环；hello 使用统一 MessageCodec 发送。
  - 代码注释要求：解释 hello 是会话初始化消息，而 Codex 查询仍保持完全被动；日志脱敏边界需写在诊断模块附近。
  - 完成标准：CLI 可加载配置、连接 ADV、发送 hello、消费请求、返回响应、断线重连和优雅退出；空闲时不查询 Codex；日志不输出凭证或完整认证响应。
  - 自动化验证：`cd desktop && uv run pytest tests/test_desktop_integration.py`，通过内存/假 BLE 与假 Provider 验证 hello、一次查询、无请求零查询、断线清理和模块异常隔离；再运行全部桌面测试。
  - 人工验证关注点：用户按 README 在实际电脑启动服务，确认配置路径、日志、Ctrl+C 退出和真实运行体验。
  - 执行记录：代码审查修复后 `uv run pytest tests/test_desktop_integration.py` 共执行 7 个内存集成用例，补充真实 BUSY 路径和空闲分帧超时；最终 `uv lock --check`、完整 67 项桌面测试及 CLI help 均以退出码 0 完成。真实 BLE 与 Codex 账号未在本任务自动调用。
  - 待确认问题：无

## Cardputer-Adv 固件

- [x] task-08: [固件基础] 建立正式 PlatformIO 工程和硬件适配入口

  - 追踪需求：R-01、R-02、NFR-01、NFR-03
  - 依赖任务：task-01
  - 修改范围：`firmware/platformio.ini`、`firmware/src/main.cpp`、`firmware/src/platform/{display_adapter,keyboard_adapter,monotonic_clock}.*`、`firmware/README.md`
  - 公共能力处理：迁移前期试验已验证的 M5Cardputer 1.2.0 初始化、横屏显示、TCA8418 键盘、Fn 层识别和字体设置；不运行时依赖试验工程。
  - 代码注释要求：只注释 Cardputer-Adv 特有初始化、Fn 事件转换和非阻塞主循环意图。
  - 完成标准：正式固件可启动并通过适配接口产生键盘事件、绘制基础画面和读取单调时间；业务层不直接调用 M5Cardputer。
  - 自动化验证：`pio run -d firmware`；该命令只证明目标固件可编译，硬件行为需人工确认。
  - 人工验证关注点：用户刷机后确认屏幕方向正确，普通键、Fn+数字、Fn+方向和 Enter 均能被识别。
  - 执行记录：建立 `firmware/` 正式 PlatformIO 工程及 Display、Keyboard、MonotonicClock 适配器，业务层不直接引用 M5Cardputer；ESP32-S3 目标构建成功。屏幕方向与实体键盘行为仍待用户刷机确认。
  - 待确认问题：无

- [x] task-09: [固件核心] 实现消息 Codec、Router 与 ConnectionSession

  - 追踪需求：R-04、R-08、NFR-01、NFR-02、NFR-04
  - 依赖任务：task-01、task-08
  - 修改范围：`firmware/src/core/{message_codec,message_router,connection_session,protocol_constants}.*`、`firmware/test/test_protocol/*`
  - 公共能力处理：新建并使用共享 fixtures；只实现 system.hello 和 codex.usage.read 所需字段，不预实现其他模块协议。
  - 代码注释要求：解释 hello 前为何保持业务断开态、为何断开清除电脑业务数据、为何迟到 execId 不能改变当前请求。
  - 完成标准：解析/编码 4 KiB 内 JSONL 业务对象，校验协议版本和基础字段，按 actionId 分发，维护当前会话并在断开时清空。
  - 自动化验证：`pio test -d firmware -e native -f test_protocol`，覆盖 fixtures、未知字段、缺失字段、不兼容版本、未知 action、断开清理和 execId；再执行固件目标编译。
  - 人工验证关注点：无需单独真机验证，最终在 task-14 联调。
  - 执行记录：实现协议常量、ArduinoJson Codec、actionId Router 与 hello 会话边界；`test_protocol` 执行 6 个 native 用例，覆盖共享 fixtures、未知字段、非法外层、未知 action、execId、版本拒绝与断开清理。
  - 待确认问题：无

- [x] task-10: [固件 BLE] 实现 GATT Server 与 JSONL 分片传输

  - 追踪需求：R-04、NFR-01、NFR-02、NFR-03、NFR-04
  - 依赖任务：task-08、task-09
  - 修改范围：`firmware/src/platform/ble_transport.*`、`firmware/src/core/jsonl_buffer.*`、`firmware/test/test_jsonl_buffer/*`
  - 公共能力处理：迁移参考项目 `BleReceiver` 的 ESP32 Arduino GATT Server、双特征、断线重播、20 字节 notify 和有限队列思路；换用独立 UUID，并改为 bytes 完整行组装。
  - 代码注释要求：解释 BLE 回调只入队不处理业务、分片间隔、缓存上限及溢出清理原因。
  - 完成标准：ADV 广播独立服务；接收 PC write 分片并产出完整消息；通过 notify 发送 ADV 请求；断开后清缓冲并重新广播；队列满时可诊断且不重启。
  - 自动化验证：`pio test -d firmware -e native -f test_jsonl_buffer` 覆盖拆包、粘包、中文跨片、超时和 4 KiB 上限；`pio run -d firmware` 兜底编译。真实 GATT 需人工确认。
  - 人工验证关注点：用户用电脑端 `--list-ble` 发现设备，确认连接、断开后重播和再次连接。
  - 执行记录：实现独立 Service/双特征 GATT Server、回调入队、有限队列、断线重播、20 字节 notify 与 4 KiB JSONL bytes 缓冲；`test_jsonl_buffer` 执行 4 个 native 用例。真实发现、连接、重播及 notify 仍待用户确认。
  - 待确认问题：无

- [x] task-11: [固件框架] 实现导航、顶部栏和模块占位页

  - 追踪需求：R-01、R-02、NFR-02、NFR-03、NFR-04
  - 依赖任务：task-08、task-09
  - 修改范围：`firmware/src/core/navigation_service.*`、`firmware/src/application/{app_shell,placeholder_page}.*`、`firmware/test/test_navigation/*`
  - 公共能力处理：扩展 task-08 Keyboard/Display Adapter；图标使用 M5GFX 绘图原语或本地位图，不引入 Emoji 字体或通用 UI 框架。
  - 代码注释要求：解释全局 Fn 组合优先于模块按键、断开态为何仍允许切换模块高亮。
  - 完成标准：开机默认 Codex；Fn+1～4 直达，Fn+左右循环；顶部四图标、第五预留和 BLE 图标布局存在；断开时四模块统一中文等待提示，三个非 Codex 模块显示中文占位。
  - 自动化验证：`pio test -d firmware -e native -f test_navigation` 覆盖直达、循环、边界和断开按键抑制；`pio run -d firmware` 验证可编译。像素效果不能由该测试确认。
  - 人工验证关注点：用户确认图标辨识度、顶部栏高度、字号、中文提示、按键手感和各模块高亮；反馈仅影响 UI 参数时可在实现阶段微调，改变交互则回到设计。
  - 执行记录：实现默认 Codex、Fn 直达/循环、20 px 顶部栏、四个绘图图标、第五预留位、BLE 图标、统一断开提示及三个中文占位页；`test_navigation` 执行 4 个 native 用例。像素效果与按键手感待真机确认。
  - 待确认问题：无

- [x] task-12: [固件 Codex] 实现用量状态模型与 ADV 前台刷新策略

  - 追踪需求：R-06、R-07、R-08、NFR-02、NFR-03、NFR-04
  - 依赖任务：task-09、task-11
  - 修改范围：`firmware/src/application/codex/{codex_controller,codex_usage_state,refresh_policy}.*`、`firmware/test/test_codex_refresh/*`
  - 公共能力处理：扩展 MessageRouter、ConnectionSession、NavigationService 和 MonotonicClock；刷新规则只存在于 ADV，不在电脑端重复实现。
  - 代码注释要求：重点解释离开 Codex 页停止周期、离页后迟到响应只更新缓存、回页按新鲜度决定查询、在途互斥和断线清理。
  - 完成标准：hello 后无数据立即查询；Enter 手动查询；Codex 页默认 300 秒自动查询；离页停止；回页按新鲜度恢复；同一时刻只有一个 execId 在途；断开清空全部电脑数据。
  - 自动化验证：`pio test -d firmware -e native -f test_codex_refresh`，使用假时钟覆盖手动、自动、缩短测试间隔、离页、回页、迟到响应、失败节奏、BUSY、millis 回绕和断线。
  - 人工验证关注点：用户联调时观察电脑日志，确认 Codex 页外没有新查询；连续按 Enter 时只产生一次在途请求。
  - 执行记录：实现会话级用量状态、在途 execId、hello 首次查询、Enter/前台周期刷新、离页暂停、回页新鲜度、迟到响应隔离、失败缓存与 millis 回绕；`test_codex_refresh` 执行 7 个 native 用例。真实 5 分钟节奏与电脑日志待联调确认。
  - 待确认问题：无

- [x] task-13: [固件 UI] 实现尽量中文的 Codex 用量页面

  - 追踪需求：R-09、R-10、NFR-03、NFR-04
  - 依赖任务：task-11、task-12
  - 修改范围：`firmware/src/application/codex/codex_page.*`、必要的 `firmware/src/platform/display_adapter.*`、`firmware/README.md`
  - 公共能力处理：复用前期真机验证的 M5GFX `efontCN_16` 和 AppShell；只抽取重复的进度条/相对时长绘制，不建立通用组件库。
  - 代码注释要求：解释百分比裁剪、缺失字段降级、相对时间基于本次开机单调时钟，以及动态名称缺字时的回退。
  - 完成标准：动态展示单/多窗口、百分比、进度条、重置倒计时和更新时间；超过两个窗口可上下滚动；等待、查询中、正常、未登录、失败、缓存尽量使用中文；缺失或异常值不越界、不空白、不重启。
  - 自动化验证：状态到 ViewModel/格式化函数使用 native 测试覆盖单/多窗口、空字段、越界百分比、相对时间和滚动边界；`pio run -d firmware` 检查 Flash/RAM 报告。像素与字形质量只能人工确认。
  - 人工验证关注点：基础中文字体已验证；用户继续在真机确认完整业务文案、图标、字号、进度条、缺字、截断、滚动和刷新视觉反馈，并记录是否接受；需要更换整体字库方案时回到设计阶段。
  - 执行记录：实现单/多窗口 ViewModel、百分比裁剪、进度条、相对重置/更新时间、双行视口滚动、中文状态与不可显示动态名称降级；`test_codex_page` 执行 5 个 native 用例。最终构建 RAM 14.9%、Flash 43.6%，字形和布局仍待用户真机确认。
  - 待确认问题：无

## 集成与交付

- [x] task-14: [集成] 完成双端自动化回归、运行文档与人工验收清单

  - 追踪需求：R-01～R-10、NFR-01～NFR-04
  - 依赖任务：task-07、task-10、task-13
  - 修改范围：`README.md`、`desktop/README.md`、`firmware/README.md`、本规格验收记录，以及仅为修复集成缺陷所需的已实现模块文件
  - 公共能力处理：复用所有前置任务；不得在此新增范围外业务。若发现协议或架构缺陷，先回到对应规格阶段更新并确认。
  - 代码注释要求：只在修复集成边界时补充解释业务原因的必要注释，不写“临时修复”等无信息注释。
  - 完成标准：提供从零安装 Python 3.12/uv、配置、运行电脑端、构建/刷写固件、查看日志、常见 macOS BLE/Codex 错误和停止程序的说明；人工清单覆盖全部验收状态。
  - 自动化验证：运行 `cd desktop && uv run pytest`、全部 firmware native tests、`pio run -d firmware`、协议 fixtures 校验和文档命令静态核对；分别记录命令、结果和未覆盖边界。
  - 人工验证关注点：用户负责实际电脑端启动、蓝牙权限、发现/连接/重连、真实 Codex 用量、Enter、Codex 页 5 分钟刷新、离页停止，以及真机全部 UI 效果；用户回传结果后再更新验收结论。
  - 执行记录：新增根运行说明和分场景人工验收清单，补齐 uv/Python、Codex 登录、配置、BLE、PlatformIO 构建/刷写、日志、停止与排障命令；修复固件广播名 `Cardputer-Adv` 与桌面端名称回退不一致、拒绝 hello 后旧缓存可能复现、长动态名称可能覆盖百分比三项集成缺陷。`uv sync --locked`、`uv lock --check`、桌面 67 项 pytest、固件 27 项 native 测试、协议 fixtures、CLI help、文档命令静态核对和 ESP32-S3 构建均以退出码 0 完成；最终 RAM 48,852 bytes（14.9%）、Flash 1,458,589 bytes（43.6%）。真实 BLE、Codex 账号及真机 UI 结论仍待用户按清单填写。
  - 待确认问题：无

## 待确认问题

无。
