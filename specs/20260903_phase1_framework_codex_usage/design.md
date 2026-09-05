> 状态：已确认

# 第一期整体框架与 Codex 用量查询：技术设计

## 1. 方案概述

第一期采用“ADV 主动调度、电脑被动执行”的双端架构：

```text
Cardputer-Adv                                      macOS 电脑端
┌─────────────────────────────┐                  ┌─────────────────────────────┐
│ Codex 页面处于前台           │                  │ BLE Central                │
│  ├─ Enter 手动触发           │  request         │  → Message Router          │
│  └─ 5 分钟前台定时触发       ├─────────────────>│  → Codex Module            │
│                             │  response        │  → Codex App Server Client │
│ 状态模型 ← Router ← BLE     │<─────────────────┤  → 本机 codex app-server   │
└─────────────────────────────┘                  └─────────────────────────────┘
```

电脑端不维护 Codex 刷新定时器，不订阅用量变化并主动推送。它可以保持 BLE 和 App Server 进程待命，但只有收到 `codex.usage.read` 才访问账户与额度接口。

选择该方案的原因：

- 查询生命周期与用户当前看到的页面一致，离开 Codex 页面后不产生后台用量查询。
- `Enter` 手动刷新和自动刷新复用同一请求路径，状态和测试更简单。
- 电脑端保持无界面、被动响应，后续新增模块时不会把多个业务定时器集中到后台进程。
- Codex App Server、BLE 和设备 UI 均通过适配接口隔离，可分别测试和替换。

## 2. 工程结构

### 2.1 计划目录

```text
firmware/
├─ platformio.ini
├─ include/
└─ src/
   ├─ application/       # 页面与业务模块
   ├─ core/              # 导航、路由、会话、协议、状态
   ├─ platform/          # BLE、显示、键盘、单调时钟
   └─ main.cpp

desktop/
├─ pyproject.toml
├─ config.json
├─ src/adv_helper/
│  ├─ application/       # Codex Module、应用装配
│  ├─ core/              # 协议、路由、会话、配置、Registry
│  ├─ platform/          # BLE Transport、诊断日志
│  └─ os_adapters/       # Codex App Server 进程适配
└─ tests/

protocol/
└─ fixtures/             # 双端共用的协议 JSON 样例
```

不复用根目录空的 `src/` 作为正式固件入口，避免与 PlatformIO 工程根混淆；正式固件集中放入 `firmware/`。

### 2.2 固件技术栈

- PlatformIO + Arduino C++。
- `M5Cardputer` 继续使用已验证的 1.2.0 版本。
- BLE 外设采用 ESP32 Arduino 内置 BLE GATT Server，并封装为 `BleTransport`。该路线已在参考项目真机使用，可迁移其广播、双特征、断线重播和分片处理方式，第一期不额外引入 NimBLE-Arduino。
- JSON 使用 ArduinoJson，并在协议层设置明确容量和消息长度上限。
- 中文使用前期真机试验确认可显示的 M5GFX `fonts::efontCN_16`；业务页面仍需验证完整文案、截断、绘制性能和资源占用，只在这些结果不满足要求时回到规格阶段调整。

### 2.3 电脑端技术栈

- Python `>=3.12,<3.14`，使用 `uv` 安装 Python 3.12、管理环境并锁定依赖；该范围沿用参考项目在 macOS/CoreBluetooth 下的实践边界。
- `asyncio` 作为单进程事件循环，统一承载 BLE 连接、消息处理和 App Server 子进程 I/O。
- Bleak 作为 BLE Central 跨平台接口；首版约束 `bleak>=3.0.2,<4`，本期只验收其 macOS CoreBluetooth 后端。
- 使用标准库 dataclass 和显式校验函数承载配置与边界数据，沿用参考项目模式，不为当前规模额外引入 Pydantic。
- pytest + pytest-asyncio 用于异步单元与集成测试。

选择 Python 的主要原因是用户已有同类项目验证了 Python + Bleak + `asyncio` 在目标 Mac 与 M5 设备间的可行性；同时 `asyncio` 能直接管理 Codex App Server 的 JSONL stdio，本期不需要 GUI 或 Node 原生 BLE 插件。

## 3. 双端模块设计

### 3.1 ADV 模块

```text
Application
├─ AppShell                 顶部栏、当前模块、通用断开页
├─ CodexPage                用量列表与状态渲染
├─ PlaceholderPage          脚本/剪贴板/设置占位
└─ CodexController          输入、刷新调度、状态变更

Core
├─ NavigationService        Fn 导航和页面切换
├─ MessageCodec             消息校验与 JSON 编解码
├─ MessageRouter            actionId 路由
├─ ConnectionSession        hello、电脑信息、会话清理
├─ CodexUsageState          Codex 页面稳定状态模型
└─ RefreshPolicy            前台定时与请求互斥

Platform
├─ BleTransport             GATT、分片、组装、发送结果
├─ DisplayAdapter           M5GFX 绘制
├─ KeyboardAdapter          M5Cardputer 键盘事件
└─ MonotonicClock           基于 millis 的时长计算
```

主循环只做非阻塞轮询和事件派发。BLE 回调不直接绘图或解析完整业务逻辑，而是把完整帧放入有限队列，由主循环消费。

### 3.2 电脑端模块

```text
Application
├─ Bootstrap                装配依赖并运行事件循环
├─ ModuleManager            模块启停与故障隔离
└─ CodexModule              被动处理 codex.usage.read

Core Services
├─ MessageCodec / Router    校验、路由、错误映射
├─ ConnectionSession        当前 ADV、协议与能力
├─ ConfigService            严格 JSON 配置
├─ ActionRegistry           actionId 到处理器
└─ QueryGate                防御性查询互斥

Platform / OS Adapters
├─ BleakTransport           扫描、连接、收发、退避
├─ LocalDiagnostics         结构化本地日志
└─ CodexAppServerClient     子进程与 JSONL RPC
```

`CodexModule` 只依赖 `RateLimitProvider` 协议，不知道 Codex 子进程细节。`CodexAppServerClient` 实现该协议，并将外部响应转换为内部 `UsageSnapshot`。

## 4. BLE 与业务协议

### 4.1 GATT 设计

使用一个固定 128-bit Service UUID 和两个 Characteristic UUID：

- `ADV → PC`：notify 特征，发送 ADV 请求；电脑端连接后订阅。
- `PC → ADV`：write/write-without-response 特征，发送电脑响应和会话 hello；电脑端首版使用 write-with-response 顺序写入。

UUID 作为项目协议常量统一定义在双端，不使用设备 MAC 作为身份。新项目生成独立 Service/Characteristic UUID，不复用参考项目的 `6e40000x-...`，避免两类设备误匹配。macOS 侧按 Service UUID 扫描并保存 CoreBluetooth 返回的设备 UUID；设备显示名称只作提示，不作唯一匹配条件。

### 4.2 帧格式

沿用参考项目已运行的 JSON Lines 思路。每条业务消息序列化为不含格式化换行的紧凑 UTF-8 JSON，并追加一个 `\n`：

```text
UTF-8 JSON | 0x0A
```

Transport 首版按 20 字节安全载荷切片并顺序发送。接收端先累计原始 bytes，发现 `0x0A` 后才解码整行 UTF-8 并解析 JSON；禁止逐 BLE 分片解码字符串，以免中文跨分片时损坏。

边界：

- 单条 JSON 最大 4096 字节。
- JSON 行组装超时或缓存溢出时丢弃当前行并记录协议错误。
- 同一方向同一时刻只发送一条分片消息，避免片段交错。
- PC 写 ADV 使用 Bleak `response=True`；ADV notify 请求保持小消息、每片间隔 10 ms。业务层不自动重发，超时后由页面提示用户决定是否按 Enter 重试。

### 4.3 会话握手

BLE 连接建立后，电脑端可以主动发送一次系统级 hello；“电脑端被动”只约束 Codex 查询，不禁止连接初始化：

```json
{
  "event": "response",
  "actionId": "system.hello",
  "execId": "8f21c7a0d1524e31",
  "result": {
    "code": "OK",
    "msg": "session ready",
    "data": {
      "protocolVersion": 1,
      "computerId": "local-stable-id",
      "computerName": "Naaran Mac",
      "capabilities": ["codex.usage.read"],
      "settings": {
        "codexRefreshIntervalSeconds": 300
      }
    }
  }
}
```

ADV 在 hello 校验成功前，UI 仍按断开状态展示。协议版本不支持时清空会话数据并拒绝业务操作。

### 4.4 Codex 查询请求与响应

请求：

```json
{
  "event": "request",
  "actionId": "codex.usage.read",
  "execId": "4f72a01c9e8b36d1",
  "payload": {}
}
```

成功响应：

```json
{
  "event": "response",
  "actionId": "codex.usage.read",
  "execId": "4f72a01c9e8b36d1",
  "result": {
    "code": "OK",
    "msg": "usage loaded",
    "data": {
      "fetchedAtEpochSeconds": 1788436800,
      "windows": [
        {
          "limitId": "codex",
          "limitName": null,
          "windowKind": "primary",
          "usedPercent": 42,
          "windowDurationMins": 300,
          "resetsAtEpochSeconds": 1788444000
        }
      ]
    }
  }
}
```

稳定结果代码：

- `OK`：查询成功。
- `NOT_LOGGED_IN`：本机 Codex 没有可用的 ChatGPT 登录。
- `BUSY`：已有意外的查询请求在执行。
- `CODEX_UNAVAILABLE`：CLI 缺失、无法启动或协议不兼容。
- `TIMEOUT`：App Server 请求超时。
- `INVALID_RESPONSE`：App Server 返回无法转换的数据。
- `ERROR`：其他可恢复错误。

每个响应必须回传原始 `execId`。ADV 只接受当前会话中正在等待的 `execId` 作为本次查询完成信号；迟到响应可记录但不得覆盖新请求状态。

## 5. Codex 查询与数据转换

### 5.1 App Server 生命周期

`CodexAppServerClient` 在第一次 ADV 查询时惰性启动：

1. `spawn("codex", ["app-server"])`，使用默认 stdio JSONL。
2. 发送一次 `initialize`，收到结果后发送 `initialized`。
3. 调用 `account/read` 判断账户状态。
4. 仅在具备 ChatGPT/Codex 服务认证时调用 `account/rateLimits/read`。
5. 按 JSON-RPC `id` 关联结果；不将普通通知当作业务响应。

子进程可以在 BLE 会话期间保持空闲，但不会自行查询。子进程退出后本次请求返回错误，不做隐藏的服务调用重试；下一次 ADV 请求可以重新拉起。

使用 stdio 是因为官方将其定义为默认 JSONL 传输；本期不启用仍属实验且不受生产支持的 App Server WebSocket 监听。

### 5.2 多额度桶转换

- `rateLimitsByLimitId` 存在时按 key 稳定排序并展开每个桶的 `primary`、`secondary`。
- 多桶视图不存在时回退到 `rateLimits` 单桶视图。
- 空窗口不生成列表项。
- `usedPercent` 在电脑端要求为整数，ADV 绘制前再次限制到 `0..100`；发生越界时记录诊断并展示安全值。
- `windowDurationMins` 或 `resetsAt` 缺失时仍展示用量，时长或重置位置显示 `--`。
- 本期忽略 credits、spend control、earned reset 等非用量展示字段。

## 6. ADV 刷新状态机

### 6.1 状态

```text
Disconnected
ReadyEmpty
Loading(no cache)
Fresh(data)
Stale(data + last error)
NotLoggedIn
Error(no cache)
```

`inFlightExecId` 与页面状态分开保存，避免“有缓存数据但正在刷新”无法表达。刷新中有缓存时继续显示数据，并在页脚显示刷新标记。

### 6.2 触发规则

| 事件 | 行为 |
| --- | --- |
| hello 完成，当前是 Codex 页且无数据 | 立即请求 |
| Codex 页按 `Enter`，当前无在途请求 | 立即请求并重置下一周期基准 |
| Codex 页保持前台且距上次查询完成达到间隔 | 自动请求 |
| 查询尚未完成时按 `Enter` 或定时到期 | 不发送新请求，保留刷新中状态 |
| 离开 Codex 页 | 取消页面刷新截止时间，不发送新请求 |
| 在途响应于离页后返回 | 更新当前会话缓存，但不启动新计时 |
| 返回 Codex 页且数据/最近尝试已超间隔 | 立即请求 |
| 返回 Codex 页且数据仍新鲜 | 展示缓存，按剩余间隔设置截止时间 |
| BLE 断开 | 清除缓存、在途 ID、电脑信息和截止时间 |

刷新间隔以查询响应完成时的设备单调时间为基准。这样慢查询不会让下一周期紧接着启动，也不依赖 ADV 不具备的持久 RTC。

### 6.3 时间展示

电脑响应提供 Unix 秒时间；ADV 收到后结合本次接收时的单调时间计算：

- Reset：显示 `2h 18m` 一类相对倒计时。
- Updated：显示 `now`、`3m ago` 一类相对时间。

本期不显示 `10:32` 这类本地时钟，以免引入时区和设备 RTC 依赖。

## 7. UI 设计

### 7.1 页面框架

- 顶部栏高度先按 20 px 实现；左侧四个 14–16 px 内置图形，第五位置留空，右侧固定 BLE 图标。
- 当前模块使用反色底块；图标全部由绘图原语或固件位图生成，不依赖 Emoji。
- 内容区通过页面接口渲染，AppShell 负责断开态覆盖和顶部栏。

### 7.2 Codex 页面

- 每个额度窗口使用一行标题/百分比、一行进度条和重置倒计时。
- 240×135 屏幕默认同时展示两个窗口；超过两个时使用 `↑` / `↓` 滚动，顶部栏不动。
- `Enter` 只在已握手的 Codex 页触发刷新。
- 页脚显示更新时间或错误短标签；有缓存的错误状态保留原数据并标记 stale。
- 页面和固定状态文案尽量使用中文；仅在术语、空间或字形确实不适合时使用英文短标签。
- 动态额度名称优先显示服务返回名称；缺失或出现不可显示字形时，回退为可识别的窗口时长或安全英文标识，不能静默显示为空白。

## 8. 配置设计

电脑端默认配置 `desktop/config.json`：

```json
{
  "configVersion": 1,
  "settings": {
    "codex": {
      "refreshIntervalSeconds": 300,
      "requestTimeoutSeconds": 15
    }
  },
  "actions": [
    {
      "type": "codex",
      "actionId": "codex.usage.read",
      "name": "Codex usage",
      "key": null,
      "enabled": true,
      "content": "usage",
      "params": {}
    }
  ]
}
```

约束：

- `refreshIntervalSeconds` 第一版允许 `60..3600`，默认 300；电脑只同步该值，不据此启动定时器。
- `requestTimeoutSeconds` 仅供电脑端等待 App Server 响应，允许 `5..60`，默认 15。
- `codex.usage.read` 被禁用时不注册，hello 不声明该 capability，ADV 显示不可用而不启动刷新。
- 实际配置不包含注释，凭证不属于配置模型。

## 9. 公共能力复用评估

### 9.1 检索范围

- 根目录、前期键盘与字体试验、本地前期准备材料（不随仓库提交）。
- 用户提供的 `/Users/naaran/Github/m5stickc-plus-pc-monitor`，重点检查 `sender/main.py`、`sender/config.py`、`sender/pyproject.toml` 与固件 `BleReceiver`。
- 已安装的 M5Cardputer/M5Unified 示例与字体定义。
- 本机 Codex CLI App Server 帮助和由当前版本生成的协议 Schema。

### 9.2 可复用能力

- 复用前期试验已验证的 M5Cardputer 初始化、键盘矩阵读取与 Fn 层识别思路。
- 复用前期真机验证的 M5GFX `efontCN_16`，不在首轮引入额外字体引擎。
- 复用官方 Codex CLI 进程和现有登录状态，不实现独立 OAuth 或读取凭证文件。
- 复用 Bleak 的 CoreBluetooth 适配，不直接编写 PyObjC BLE 实现。
- 迁移参考项目按 Service UUID 优先扫描、无结果时回退扫描、连接后等待 1000 ms、GATT discovery 5 秒超时/3 次有限重试、设备名或 CoreBluetooth UUID 辅助选择的策略。
- 迁移参考项目动态补充 `NSBluetoothAlwaysUsageDescription` 的处理及清晰的 macOS 蓝牙权限错误提示。

### 9.3 差距与处理决策

- 试验键盘代码没有事件抽象：新建 `KeyboardAdapter`，只上报稳定按下事件和修饰键组合。
- 没有消息、路由、会话或状态公共能力：按双端最小闭环新建，接口只覆盖 hello 和 Codex 查询。
- 没有可用 BLE 传输：新建 GATT Transport；分片属于 Transport，不放入业务模块。
- 参考项目 BLE Transport 与当前仓库没有可直接导入的 Python package 边界，因此采用“提炼迁移并补测试”，不让新项目运行时依赖另一个仓库。
- 参考项目使用相同 Nordic UART UUID 会造成设备冲突，新建独立 UUID；参考项目逐 notify 分片解码 UTF-8 的方式改为 bytes 累积后按完整 JSONL 解码。
- 没有 App Server 客户端：新建独立适配器；只实现 initialize、account/read、account/rateLimits/read。
- 没有共享协议代码生成链：第一期用协议常量和 JSON fixtures 做跨语言契约测试，不建立复杂生成器。

## 10. 实现约束

- 生产代码只实现本设计列出的首期能力，不为脚本、剪贴板和设置预建具体业务流程。
- 业务规则只保留一份：刷新状态机在 ADV，查询互斥以 ADV 为主、电脑端防御性兜底。
- 对“为什么离页停止定时”“为什么迟到响应不能覆盖新状态”“为什么断开清缓存”等关键边界补充业务意图注释。
- 单次使用的简单字段转换留在适配器中；仅消息编解码、状态机和平台边界形成公共抽象。
- 日志不得输出 Codex 凭证、完整认证响应或用户敏感配置。
- 前期试验只作为实现依据，不作为正式工程的运行时依赖。

## 11. 验证设计

### 11.1 电脑端自动化

- 配置：默认值、范围、未知字段、单项非法隔离、禁用 action。
- 协议：请求校验、execId 原样响应、错误代码映射、JSONL 拆包/粘包、中文 UTF-8 跨分片。
- App Server：用假 JSONL 子进程验证 initialize、账户读取、单桶、多桶、超时、退出和畸形响应。
- 被动性：没有 ADV 请求时不调用 provider；每个有效请求最多调用一次额度读取；并发请求返回 BUSY。
- BLE：使用内存 Transport 测路由和断线会话清理。

### 11.2 固件自动化

- 将 Navigation、RefreshPolicy、MessageCodec 和 CodexUsageState 保持为无硬件依赖 C++，使用 PlatformIO native 测试。
- 使用可控单调时钟验证 Enter、300 秒前台刷新、离页停止、回页过期刷新、在途互斥和断开清理。
- 使用共享 fixtures 验证 hello、成功、未登录、错误、单/多窗口与未知字段。
- 固件目标编译作为基础兜底，不替代上述逻辑测试。

### 11.3 人工验证

1. 真机刷入固件，检查四模块导航、顶部高亮、BLE 图标和断开提示。
2. macOS 首次运行电脑端，授权蓝牙，完成扫描、连接、hello 和断线重连。
3. 在 Codex 页按 `Enter`，确认只产生一次查询并展示真实额度。
4. 保持 Codex 页，观察配置的缩短测试间隔触发；切到其他页确认电脑日志不再出现查询。
5. 在查询过程中离页，确认返回结果不会启动后台周期；回页按新鲜度决定是否查询。
6. 注销或破坏 Codex 可用性，确认未登录/失败/缓存状态，再恢复并手动刷新。
7. 检查固定中文状态文案、Flash/RAM 报告与绘制清晰度。

人工验证责任边界：

- 用户负责 Cardputer-Adv 真机上的图标、字号、进度条、中文状态文案、导航和页面效果确认。
- 用户负责电脑端实际启动、macOS 蓝牙权限、设备发现、连接、断线重连和真实 Codex 账号查询确认。
- 实现阶段由 Codex 完成不依赖上述人工操作的自动化测试、固件编译和静态检查，并提供清晰的人工验证命令与步骤。

用户回传人工结果前，只报告自动化与构建结果，不宣称端到端或真机验收通过。

## 12. 需求追踪

| 需求 | 设计覆盖 |
| --- | --- |
| R-01 | 2.1、2.2、3.1 |
| R-02 | 3.1、7.1 |
| R-03 | 2.1、2.3、3.2 |
| R-04 | 4.1～4.3 |
| R-05 | 4.3、8 |
| R-06 | 4.4、5 |
| R-07 | 6 |
| R-08 | 4.4、5.2 |
| R-09 | 6.1、6.3、7.2 |
| R-10 | 2.2、7.2、11.3 |
| NFR-01 | 2、3、9、10 |
| NFR-02 | 4～6、10 |
| NFR-03 | 3.1、6、7 |
| NFR-04 | 11 |

## 13. 主要风险与规避

- **macOS 蓝牙权限**：首次扫描会触发系统授权；捕获权限异常并指引系统设置，人工验收记录实际启动入口。
- **Python/CoreBluetooth 版本组合**：沿用参考项目的 Python `>=3.12,<3.14` 约束，由 uv 锁定 3.12；启动时检测并拒绝已知不稳定的 3.14 运行时。
- **CoreBluetooth 无公开 MAC 地址**：按扫描到的 BLEDevice/CoreBluetooth UUID 工作，不依赖 MAC 字符串。
- **Codex App Server 演进**：固定最低 Codex CLI 版本，启动时握手；外部结构只存在于适配器，关键响应使用假服务契约测试。
- **App Server 是实验集成面**：使用默认 stdio 而非实验 WebSocket；失败时明确提示电脑端升级或重新登录，不让固件承担兼容逻辑。
- **BLE 分片与内存**：限制 4 KiB、单方向串行、超时清缓冲；用边界和畸形帧测试防止内存持续增长。
- **设备无持久 RTC**：页面只显示相对重置与更新时间，使用响应 epoch 和本次开机单调时钟计算。
- **完整中文页面仍有不确定性**：`efontCN_16` 的基础文字已真机验证，但完整业务文案的缺字、截断、刷新速度和资源占用仍需用户确认；不满足时按需求变更流程确认降级。

## 14. 参考资料

- OpenAI Codex App Server：<https://developers.openai.com/codex/app-server/>
- Bleak macOS backend：<https://bleak.readthedocs.io/en/develop/backends/macos.html>
- M5Cardputer 官方库：<https://github.com/m5stack/M5Cardputer>
- 本地参考实现：`/Users/naaran/Github/m5stickc-plus-pc-monitor`

## 15. 待确认问题

无。
