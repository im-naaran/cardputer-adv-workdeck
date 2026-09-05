> 状态：已确认

# 第一期整体框架与 Codex 用量查询：项目上下文

## 1. 项目现状

> 本文记录第一期启动前的上下文；文中键盘和字体试验工程已在正式能力迁移完成后移除。

项目当前处于正式工程启动前阶段。本地前期需求草稿（不随仓库提交，正式需求见本规格 requirements.md）已描述 ADV 工作台的完整产品方向、分层架构、BLE 消息模型和四个模块，但正式固件与电脑端工具尚未建立。

当时已有键盘和字体独立试验工程，分别用于 M5Stack Cardputer-Adv 屏幕/键盘及多语言字体真机验证；这些试验不属于正式固件。

## 2. 已识别技术栈

### 2.1 ADV 固件

- 目标设备：M5Stack Cardputer-Adv（ESP32-S3）。
- 构建方式：PlatformIO + Arduino。
- 已试验平台：`espressif32@6.7.0`。
- 已试验设备库：`M5Cardputer` 1.2.0，由 M5Unified 提供屏幕等底层能力。
- 已验证能力：屏幕初始化、横屏绘制、TCA8418 键盘读取、Fn 层和组合键识别。
- 已验证字体：`fonts::efontCN_16` 的基础中文以及 `fonts::efontJA_16` 的基础日文已由用户在 Cardputer-Adv 真机确认可正常显示。

### 2.2 电脑端工具

- 尚无正式工程、语言、运行时或 BLE 库。
- 需求要求核心协议、配置、路由和业务模块跨平台；操作系统差异放入适配层。
- 首期只需实际完成一个电脑平台的闭环，其余平台保留清晰适配边界，首个验收平台待确认。
- 用户提供 `/Users/naaran/Github/m5stickc-plus-pc-monitor` 作为已实践的参考实现。该项目电脑端使用 Python、`asyncio` 和 Bleak，已覆盖 macOS CoreBluetooth 的扫描、连接、GATT discovery、notify、分片和重连处理，应优先复用其实现经验。

### 2.3 Codex 接入

- 本机已安装 `codex-cli 0.153.0`，提供实验性的 App Server 和协议 Schema 生成能力。
- 官方 Codex App Server 文档提供 `account/read`、`account/rateLimits/read` 和 `account/rateLimits/updated`。
- `account/rateLimits/read` 可返回单桶兼容视图及按 `limitId` 组织的多桶视图；额度窗口包含 `usedPercent`、`windowDurationMins` 和 `resetsAt`。
- 电脑端应复用本机 Codex 登录状态，不自行读取、复制或向 ADV 下发登录凭证。
- App Server 与相关接口仍有演进可能，电脑端必须通过适配器隔离 Codex 协议，并对字段缺失和多额度桶变化保持兼容。

官方参考：<https://developers.openai.com/codex/app-server/>

## 3. 目标架构上下文

需求草稿已确定 ADV 与电脑端采用对称的分层思路：

```text
Application（业务模块）
        ↓
Core Services（导航、路由、会话、配置、状态）
        ↓
Platform / OS Adapters（BLE、屏幕、键盘、进程等）
```

第一期应建立完整分层骨架，但只实现 Codex 用量业务。快捷脚本、剪贴板和设置模块只提供导航占位与注册边界，不实现其业务能力。

## 4. 现有公共能力与复用点

- 前期试验中的键盘扫描、Fn 组合键识别和“内容变化才重绘”的做法可作为固件输入与渲染实现参考。
- 前期试验中的 `fonts::efontCN_16` 选择、UTF-8 字符串绘制和字号设置可作为中文页面基线；实际业务文案的缺字、截断和资源占用仍需继续验证。
- 试验代码当前全部集中在单文件匿名命名空间内，不具备可直接复用的模块边界；正式固件应按分层职责重新组织，避免复制整份试验程序。
- 仓库没有现成 BLE Transport、Message Router、Connection Session、配置加载器、Codex 客户端、页面组件或测试设施。
- 参考项目的 `sender/main.py` 提供可迁移的 `ReconnectableBleTransport`、按 Service UUID 发现设备、连接后延迟 discovery、有限重试、macOS 蓝牙权限说明注入等实现；其 `sender/config.py` 提供集中管理 BLE 参数的方式。
- 参考项目固件的 `BleReceiver` 验证了 ESP32 Arduino 内置 BLE GATT Server、双特征双向通信、20 字节分片、JSON Lines 组装和断线重新广播。
- 参考实现不能原样复制：其 Python notify 回调逐分片 UTF-8 解码，中文跨分片时可能被替换；其 Nordic UART 形态 UUID 也不能与新设备共用，否则两类设备可能互相误匹配。
- 新增公共接口前需要再次检索正式工程中是否已有同类能力；绿地阶段以最小接口为主，不为后续未审核模块预实现业务细节。

## 5. 关键约束

- ADV 与电脑端只通过 BLE 传输业务数据；ADV 不直接访问 Codex 或公网服务。
- 业务模块不得依赖具体 BLE、显示、键盘或操作系统实现。
- 每条消息使用 `event`、`actionId`、`execId`，连接初始化使用 `system.hello` 协商协议版本和能力。
- 电脑断开后清除当前电脑的运行时业务数据，不在 ADV 上保留 Codex 凭证或跨电脑用量缓存。
- Codex 刷新间隔从统一配置读取，默认 300 秒；禁止并发查询和失败后的高频重试。
- Phase 0 至 Phase 3 只修改本规格目录；生产代码、依赖、锁文件和工程配置只能在任务拆解确认后修改。

## 6. 已知风险

- 电脑端首个目标平台和 BLE 库尚未确定；该选择会影响工程技术栈与端到端验证方式。
- App Server 属于随 Codex CLI 演进的集成面，需要固定最低支持版本、解析兼容策略和清晰错误提示。
- BLE 消息可能超过单包容量，分片、组装、超时和格式校验必须由传输层统一处理。
- Cardputer-Adv 的中文字库占用、绘制速度和缺字表现尚未真机验证。
- 当前已有键盘和基础中文试验，但没有 BLE、完整业务中文、长期刷新或跨进程异常恢复的验证基线。
- 参考项目明确限制 macOS BLE 使用 Python `>=3.12,<3.14`，并记录 Python 3.14/CoreBluetooth 不稳定；新项目需沿用该运行时边界，不能直接使用系统 Python 3.9。
