# 设置模块：项目上下文

> 状态：已确认
> 日期：2026-09-06
> 阶段：Phase 0 完成；此状态仅表示上下文分析完成，不代表需求已获批准。

## 项目现状

- 用户确认基础通信、Codex、脚本模块已完成，本次新增设置功能。
- 固件采用 C++17、Arduino、PlatformIO，目标为 ESP32-S3 Cardputer-ADV；界面使用 M5Cardputer / M5GFX，JSON 使用 ArduinoJson。
- `firmware/src/application/` 承担页面与业务，`core/` 承担输入、导航、协议与调度，`platform/` 隔离硬件及文件系统。桌面端通过 BLE 被动响应业务请求。
- 四模块入口及顶栏已存在，设置仍由 `PlaceholderPage` 绘制。`main.cpp` 在输入和绘制时统一检查 BLE 会话，新增设置必须能独立离线使用。
- 本次最新用户要求优先于旧方案：Wi-Fi 字段全部明文展示；增加 Codex 分钟配置；不自动纳入旧草稿中的 BLE 管理和版本信息。

## 已检索的公共能力与差距

| 能力 | 当前实现与本次方向 |
| --- | --- |
| 顶栏、导航、中文绘制 | 复用 AppShell、NavigationService、DisplayAdapter；维持四模块和现有视觉风格 |
| 亮度 | DisplayAdapter 尚无亮度设置接口；需要扩展平台边界 |
| 输入 | KeyboardAdapter 记录基础键位；InputRouter 消费 Shift 组合并映射 `; , . /` 为方向；文本输入需有编辑上下文及实际字符处理 |
| 配置存储 | PlatformConfigFileStore 使用 LittleFS，临时文件写入、回读校验、rename 替换；挂载失败不自动格式化；单文件当前上限 512 字节 |
| Codex 配置 | CodexConfigService 已提供 read/reload/save；文件 `/config/codex.json`，默认 300 秒，范围 60～3600 秒；设置页复用此入口 |
| Codex 调度 | 现有控制器与 ScheduledTaskService 管理自动刷新；周期变化立即更新，保留开关、缓存及在途请求 |
| Wi-Fi | 当前应用源码无扫描、连接或认证适配，需要新增；具体 API 与企业认证支持在设计阶段核对本机已解析依赖 |

## 约束与风险

- Wi-Fi 用于设备自身联网，业务继续通过 BLE；不改变桌面协议和 Codex 调度归属。
- 240×135 小屏需支持长字段查看与编辑，中文显示与键盘字符输入能力需分别验收。
- Wi-Fi 扫描、连接与超时不得阻塞主循环。若引入重复联网任务，应复用统一调度；操作超时可由本地状态机处理。
- Wi-Fi 与 BLE 同时工作、企业网络认证、持久化及断电行为需要真机验证；源码分析不能替代这些结论。
- Wi-Fi 字段及 JSON 转义可能触及现有文件大小限制，设计需明确字段长度与存储容量，不可静默截断。
- 规格阶段仅修改本规格目录；本轮不运行构建、测试、安装或刷写。

## 依据

- 本次用户指令及[设置模块需求规格](requirements.md)：离线设置亮度、Wi-Fi 配置/扫描/测试和 Codex 刷新周期；复用持久化与调度能力，BLE 管理和设备版本页不纳入本期。
- `README.md`、`firmware/platformio.ini`。
- `firmware/src/main.cpp`、`application/app_shell.cpp`。
- `platform/display_adapter.*`、`platform/keyboard_adapter.cpp`、`core/input_router.cpp`。
- `platform/config_file_store.*`、`application/codex/codex_config.*`。

## Phase 2 补充核对

- 用户指定 focus-clock 作为已成功连接公司 Wi-Fi 的参考。已读取其 `src/wifi_service.cpp` 和 `src/wifi_logic.cpp`：使用 PEAP，identity 与 username 共用账号，证书为空；切换前禁用并清理企业认证状态。
- 已核对本机 Arduino ESP32 对应包版本 `3.20017.241212+sha.dcc1105b` 的 WiFiSTA、WiFiScan 和 ESP32-S3 企业认证头文件；存在相应接口，无需升级依赖。
- 已核对 M5Cardputer 键位映射有基础字符与 Shift 字符，M5GFX 提供 uint8_t 亮度接口。
- Wi-Fi 配置按 SSID 32、用户名 64、密码 64 字节及控制字符禁用限制设计，序列化上界可容纳于现有 512 字节文件限制，不扩大存储接口。

- 最新用户边界：Wi-Fi 不展示类型，按用户名分流认证；当前仅配置、扫描和测试，固定 AUTO 按需启用。保存与开机不连接，扫描/测试结束关闭 Wi-Fi，保留结果；不引入实际联网业务。

- 用户新增公共能力边界：本期提供 WifiService 的配置检查、连接条件、异步连接、状态查询与关闭接口。Wi-Fi 服务独立于设置页，单一使用者互斥；未来模块成功连接后显式关闭，设置测试结束自动关闭。

- task-05 构建实证修正：本项目解析的是 `framework-arduinoespressif32@3.20016.0`（Arduino 2.0.16），不是早期读取的未带版本目录中的 2.0.17。已重新核对对应 WiFiSTA/WiFiScan 接口，未升级依赖。
