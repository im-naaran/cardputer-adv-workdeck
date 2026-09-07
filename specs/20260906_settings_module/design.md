# 设置模块技术设计

> 状态：已确认
> 日期：2026-09-06
> 阶段：Phase 2 已确认，进入 Phase 3 任务拆解

## 1. 方案

在现有设置入口增加设备本地页面，包含亮度、Wi-Fi、Codex 周期三个功能。页面与控制器负责交互，平台适配器负责屏幕、键盘、Wi-Fi，配置继续使用 LittleFS 模块文件。无需修改桌面端或 BLE 协议。

Wi-Fi 认证参考用户已验证的 focus-clock：普通网络使用 SSID、密码；公司网络使用 PEAP，账号同时作为 identity 和 username，证书参数全部为空。界面只展示 SSID、可选用户名及密码，按用户名是否为空决定认证路径，不保存 type 字段。Wi-Fi 固定采用 AUTO 按需策略：平时关闭，由扫描、测试或模块显式请求才启用；本期提供公共服务，尚无实际联网业务调用方。保留本项目的单主循环、模块隔离及原有顶栏。

## 2. 页面与按键（R-01～R-06）

```text
设置首页
  屏幕亮度       60%
  Wi-Fi          未配置 / 已配置（静默） / 扫描中 / 测试中
  Codex 自动刷新 5 分钟

Wi-Fi 表单（滚动显示）
  SSID           当前值
  用户名         当前值（可留空）
  密码           当前值，明文
  扫描网络
  保存
  测试连接
  查看网络信息
```

- 顶栏保持原样；首页删除“设置”标题，底部仅一行“↑↓选择 enter进入 退格返回”。子页无固定提示栏，正文五行滚动，操作结果在对应行短暂显示。
- 首页及列表用方向键或 Tab 选择，Enter 进入，Backspace 返回。
- 亮度页标题为“亮度”，方向键逐档调整并即时保存，边界停止；Tab 循环五档，Enter 不执行。失败保留当前亮度，再次调整可重试。
- Codex 子页直接调整 1～60 分钟，方向键逐分钟调整，Tab 循环递增。使用单调时间进行 600 毫秒保存防抖，main 每轮调用 tick，返回及 Fn 切模块补存；失败保留草稿与错误，不后台持续重试，下一次调整或离开时重试。无下一级编辑页、保存按钮或状态查看入口。
- Wi-Fi 三字段在当前行编辑，支持尾部追加及退格；Enter 写回草稿并结束编辑，Tab 提交并直接编辑下一字段，均不写 Flash。
- 长字段按 UTF-8 字符边界横向滚动到尾部光标，不提供凭据全文查看入口。“查看网络信息”汇总状态与测试 IP，按五行分页；关闭失败时正文四行并显示重试操作。Tab/方向键翻页，Backspace 返回，Enter 在关闭失败时重试，否则返回。
- 退格删除一个 UTF-8 字符；SSID 从扫描带入的中文也可以正确删除。首尾空格不自动裁剪；空格属于凭据内容。
- Fn+数字及 Fn+左右仍切换模块，切页保留设置子页、草稿和编辑内容。Alt 脚本快捷键仍有效，Fn+Enter 仍仅在 Codex 页面有效。
- Wi-Fi 字段及密码全部明文；空密码就是空值，不沿用 focus-clock 网页端“留空保留旧密码”语义。
- 保存结果和测试结果分开展示，例如“已保存”“上次测试成功，Wi-Fi 已关闭”。测试凭据与已保存值不同时标示“测试配置未保存”。测试 IP 仅作为历史结果，不显示为当前在线 IP；修改任一凭据后标示“配置已修改，需重新测试”，不将旧结果附到新草稿上。
- 设置页先于 BLE 离线遮罩分支绘制和处理输入；其他模块维持原有离线行为。脚本全局反馈沿用现有短暂提示，消失后恢复设置正文。

## 3. 模块与公共能力复用

| 位置 | 处理方式及职责 |
| --- | --- |
| `application/settings/settings_page.*` | 新建设置页面，负责焦点、字段编辑和绘制；不直接访问硬件或文件 |
| `application/settings/settings_controller.*` | 新建薄控制器，协调草稿、保存结果和各服务；不复制 Codex 调度 |
| `application/settings/display_config.*` | 新建亮度配置校验及保存服务，复用 ConfigFileStore |
| `application/wifi/wifi_config.*` | 新建 Wi-Fi 配置编解码、校验及保存；与连接状态分离 |
| `application/wifi/wifi_service.*` | 新建公共 WifiService，统一配置检查、扫描、连接、状态查询和关闭，持有操作快照与使用权，支持 native 测试 |
| `platform/wifi_adapter.*` | 新建小型可替换接口及 Arduino 实现，隔离 WiFi/esp_wpa2 API；native 使用 fake |
| `platform/display_adapter.*` | 扩展 `setBrightness(uint8_t)`，只在平台层调用 M5Cardputer |
| `platform/keyboard_adapter.*`、`core/input_router.*` | 扩展实际文本字符、退格/Tab 及编辑上下文，保留基础物理键身份和全局快捷键优先级 |
| `application/codex/codex_config.*` | 直接复用 read/save/reload；不新增设置页专用 Codex 文件 |
| `platform/config_file_store.*` | 复用临时文件写入、回读、rename、错误码及 512 字节限制；不为本期新增通用配置框架 |
| `main.cpp` | 注入依赖、开机加载、轮询 Wi-Fi、设置输入/绘制分派；继续串行更新业务状态 |

已检索 application、core、platform、firmware/test 及本机安装库；当前不存在可复用的文本编辑器或 Wi-Fi 公共服务。Wi-Fi 从设置私有目录独立为 application/wifi，设置页与未来模块共用同一服务实例。文本编辑仅在设置页内部实现小型状态，不建设通用表单引擎。已有文件存储与 Codex 服务满足需求，无需修改桌面配置或调度服务接口。

## 4. 配置与保存（R-02、R-04、R-06、R-07）

| 文件 | 内容示例 | 默认 |
| --- | --- | --- |
| `/config/display.json` | `{"brightnessLevel":3}` | 第 3 档，60% |
| `/config/wifi.json` | `{"ssid":"Office","username":"account","password":"example"}` | 文件不存在表示未配置 |
| `/config/codex.json` | `{"refreshIntervalSeconds":300}` | 300 秒，复用现有格式 |

不增加总配置文件，不重复保存 Codex 周期。新配置保持现有小型严格 JSON 的惯例，不在本期引入统一版本迁移机制。JSON 使用序列化器处理引号和反斜杠，拒绝非法字段类型、未知字段、尾随垃圾和超长文件。

### 校验与容量

- 亮度档位为整数 1～5，平台控制值依次为 51、102、153、204、255；布尔值和小数不能当作整数接受。
- SSID 为 1～32 字节的有效 UTF-8；用户名为 0～64 字节。用户名为空选择普通认证，非空选择 PEAP；不裁剪空格、不依据扫描结果覆盖用户名。清空用户名后重新按普通密码规则校验。
- 普通密码允许空值连接开放网络；否则为 8～63 个可打印 ASCII 字符，或 64 位十六进制 PSK。公司密码为 1～64 字节有效 UTF-8，不套用普通密码的最短 8 字符限制。
- 字段禁止 NUL、换行和其他控制字符，长度按编码后的字节数计算；超限提示并拒绝新增字符/提交，不截断。
- 最大字段合计 160 字节，允许的引号/反斜杠最多翻倍转义，加上 JSON 固定键仍小于 512 字节。编码后再次检查总长度，测试覆盖全部转义的上界；现有存储上限无需扩大。
- Codex UI 只接受 1～60 的十进制整数字符串，再乘以 60 传入现有服务。已有非整分钟值按秒如实显示，进入页面不取整保存；首次方向调整才改为整数分钟。

### 保存语义

1. Wi-Fi：校验草稿 → 与磁盘值比较 → 必要时 replace → 回读校验 → 更新已保存快照，结束；不申请 Wi-Fi 需求。写失败不更新已保存值；回读失败提示“文件可能已保存，读取失败”，不虚报成功。
2. 测试：校验草稿 → 复制为本次操作快照 → 临时启用 STA 并开始连接 → 记录结果 → 断开、清理认证并关闭 Wi-Fi；不调用配置保存服务。
3. 亮度：先应用档位保证即时反馈，再保存并回读。失败时保持实际亮度和未保存状态，用户可再次调整重试；Enter 不执行，磁盘已是相同值时跳过写入。
4. Codex：使用 `encodeCodexConfig` 和 `CodexConfigService::save`。页面区分持久化值与当前运行值；保存失败保留输入，ApplyFailed/ReloadFailed 沿用现有语义。
5. 草稿、磁盘已保存值、实际连接快照分别持有；对比全部凭据判断是否“已保存”，不能只比较 SSID。再次进入页面刷新无未保存编辑的值，不在每帧轮询文件。

### 开机顺序

显示初始化 → 挂载既有 LittleFS → 读取亮度并应用默认或保存值 → 初始化现有调度并加载 Codex 配置 → 读取 Wi-Fi 配置但保持无线关闭 → 正常主循环。尽早应用亮度，避免首次业务画面持续使用库默认值。

新文件可在已挂载文件系统上首次保存时创建，不要求已有用户为此重新 uploadfs。缺失新文件使用默认且显示未配置；损坏、读取失败和未挂载显示对应错误，不自动格式化。普通应用更新沿用既有文件系统保留条件；首次部署说明在实现任务中补充。断电发生在提交前应保留旧值，提交后的恢复结果须用真机验证，不能仅凭 rename 推断断电可靠性。

## 5. Wi-Fi 公共服务与状态机（R-03～R-05、R-08）

### 平台调用

参考 focus-clock `src/wifi_service.cpp` 的 `startSta`、`stopSta` 和异步扫描。接口依据项目实际解析的 Arduino ESP32 2.0.16 对应包及其 ESP32-S3 SDK 头文件核对，不升级依赖。

```cpp
// 公司网络，与 focus-clock 已成功连接的路径保持一致。
WiFi.begin(ssid, WPA2_AUTH_PEAP, username, username, password,
           nullptr, nullptr, nullptr);
```

- 普通网络用 `WiFi.begin(ssid, password)`；开放网络传空密码，并在适配器中显式允许开放网络的最低认证级别。
- 初始化时在启用 Wi-Fi 前设置 `WiFi.persistent(false)`，以 LittleFS 为唯一持久配置来源，防止测试凭据隐式写入驱动存储。禁用驱动自动重连，使单次超时可控。
- 仅存在扫描、测试或模块连接需求时启用 STA 模式；无需求时为 WIFI_OFF。不启动 AP、HTTP 服务或网页 Portal，也不通过关闭整个无线子系统影响 BLE。
- 切换凭据前先断开，调用 `esp_wifi_sta_wpa2_ent_disable` 并清理 identity、username、password，之后再应用新凭据。企业转普通、企业账号变更必须经过同一路径。
- 异步扫描使用 `scanNetworks(true, false)`、`scanComplete()`，结果复制后 `scanDelete()`。超时使用 `esp_wifi_scan_stop()` 停止底层扫描并完成清理；不能只改 UI 状态。
- 本机企业 begin 封装并未逐项返回底层设置结果，控制器以实际连接状态及有效 IP 判定成功；不能仅凭调用返回就显示成功。

### 状态及并发边界

分开维护配置状态 `Unconfigured / Configured`、需求 `None / Scan / Test / Module`、执行阶段 `Off / Scanning / Connecting / Connected / Releasing / ReleaseFailed`，以及测试结果 `None / Succeeded / Failed / TimedOut`。测试 SSID/IP 属于历史结果快照，测试成功不代表持续在线；模块连接则通过 Connected 状态和当前 IP 表示在线。扫描结果单独保留，避免清理无线资源时丢失可选网络。

| 事件 | 行为 |
| --- | --- |
| 开机有有效配置 | 加载配置并显示已配置；需求为 None，保持 WIFI_OFF |
| 保存 | 仅保存，不申请需求；进行中的测试继续使用原有快照 |
| 测试 | 无其他无线操作时申请 Test；使用不可变凭据快照，30 秒截止 |
| 扫描 | 无操作时申请 Scan，15 秒截止；仅扫描，不连接已保存网络 |
| 操作中再次扫描/测试 | 按钮禁用并显示正在处理，不排队、不并行；允许编辑、保存和切页 |
| 测试取得连接及非零 IP | 记录成功和 SSID/IP，立即进入 Releasing；释放后显示测试成功、Wi-Fi 已关闭 |
| 模块取得连接及非零 IP | 进入 Connected，保留使用权及连接，等待持有者 close |
| 持有者 close | 校验请求身份，取消进行中的连接或释放已建立连接，进入 Releasing |
| 模块使用中断线 | 标记 Disconnected 并释放、关闭；不自动重连，调用方按结果结束业务 |
| 明确失败/连接超时 | 记录结果并进入 Releasing，清理企业状态、关闭 Wi-Fi，保留草稿 |
| 扫描完成/失败/超时 | 复制结果或记录错误，停止扫描、释放驱动结果，进入 Releasing 并关闭 Wi-Fi |
| 切页/BLE 断线 | 操作继续，不清除设置或网络状态 |

测试从无线关闭态启用 STA 并清理旧认证状态，再开始本次认证；不得把旧 IP、旧连接或上一轮失败状态当作本轮结果。启用准备阶段计入 30 秒总时限。使用单调时钟差值处理超时并覆盖 millis 回绕测试。

Releasing 中撤销需求，停止本轮扫描或连接并清理企业认证，调用 WiFi.mode(WIFI_OFF)。确认平台返回关闭后才进入 Off；清理失败进入 ReleaseFailed，显示“Wi-Fi 关闭失败”，不能虚报静默，也不能启动下一轮无线操作。允许用户通过页面“重试关闭”执行清理，不自动重连或无间隔重试。测试结果和释放错误分别保留。

本期采用单一使用者互斥，不实现引用计数或多模块共享连接。设置扫描、设置测试、未来模块连接都通过同一个 WifiService 取得使用权；忙时直接返回 Busy，不排队、不抢占。模块成功连接后显式释放，设置扫描/测试则在结束时自动释放。服务可独立于页面使用，无需后续模块自行调用平台 WiFi API，也不增加 AUTO 模式选择或配置字段。

### 公共接口约定（R-08）

以下为接口语义，具体声明在任务实现时保持此契约；WifiService 依赖 WifiConfigService、WifiAdapter 和单调时钟，不依赖 SettingsPage。

| 接口 | 语义 |
| --- | --- |
| `configurationStatus()` | 返回 Missing / Valid / Invalid / ReadFailed / NotMounted；查询服务已加载的保存配置状态，不启用 Wi-Fi，不暴露密码 |
| `canConnect()` | 返回 Ready / NotConfigured / InvalidConfig / StorageError / Busy / ReleaseFailed；只表示能否发起尝试，不保证附近有网络或密码正确 |
| `connect()` | 使用当前有效已保存配置快照，接受时返回 requestId 并异步连接；拒绝时返回具体原因，不启用无线 |
| `status(requestId)` | 查询该请求的 Connecting / Connected / Failed / TimedOut / Disconnected / Closing / Closed 及错误；在线时附带 SSID/IP，过期请求返回 Expired |
| `isConnected()` | 查询当前实际连接且具有有效 IP 的状态，不读取上次测试结果 |
| `close(requestId)` | 仅持有者可取消连接或释放已建立连接；确认 WIFI_OFF 后才视为关闭完成，关闭失败保留 ReleaseFailed |
| `scan()` / `test(draft)` | 设置专用操作，同样走互斥控制；test 接受草稿快照但不改变已保存配置，操作结束自动关闭 |
| `tick(nowMs)` | 主循环驱动状态、超时和清理；无需求时不发起连接、不反复开关无线 |

- requestId 为本地单调递增的非零 64 位操作标识，不占用 BLE execId。释放与状态查询必须匹配，旧请求不能关闭新请求；标识耗尽时拒绝分配而非复用旧值。
- 仅保留当前/最近一次请求结果，不建设历史队列。相同已关闭请求重复 close 返回 Closed；开始新请求后旧标识返回 Expired，不改变新请求。扫描/测试也分配标识，避免与模块连接交叉。
- 配置在开机加载、显式 reload、保存成功后刷新快照，不按帧读文件。connect 发起前由配置服务重新读取并校验文件；若读取失败则拒绝本次新连接并报告原因，已有连接不受影响。canConnect 是调用时快照，不能代替 connect 的最终检查。
- 有模块持有连接时，设置仍可编辑和保存，但扫描/测试返回 Busy。保存不会替换在途凭据，下次请求才读取新值。
- 模块连接达到 30 秒超时或失败后自动释放；成功后由调用者在正常结束、异常、取消路径调用 close，不以超时任意中断已成功连接的业务。调用范式为“检查 → connect → 轮询结果 → 执行业务 → close → 确认关闭”。
- 当前只实现服务及 fake 调用模块的验证，不新增真实联网模块、永久连接入口或后台周期重连。未来若确需多个业务同时持有连接，再扩展共享需求管理。

平台适配器只提供 `startScan / pollScan / stopScan`、`disconnectAndClearAuth / startConnect / connectionSnapshot / powerOff` 等必要操作。业务在主循环轮询，不注册直接修改 UI 的异步回调，不使用 waitForConnectResult 或长 delay。原有周期任务继续由 ScheduledTaskService 管理，本期 Wi-Fi 只有一次操作超时，不新增周期任务。

### 扫描结果

使用固定容量 32 条结果，按信号强度保留较强网络；按相同 SSID 去重，保留较强结果；认证方式始终由输入的用户名决定。只复制有合法非空 SSID 的结果，隐藏网络使用手动输入。列表分页展示 SSID 和信号强度，不展示类型，选择只填写 SSID，不改用户名/密码、不自动连接或保存。结果超过容量时提示“仅显示信号较强的 32 项”，始终允许重新扫描和手动输入。此容量仅用于无线扫描内存，不影响现有脚本列表。

扫描结果在关闭 Wi-Fi 后仍可选择；测试结果在关闭后仍可查看。已保存配置不变，下次开机只回显配置，静置不会联网。

## 6. 输入与 Codex 集成（R-01、R-03、R-06）

- KeyEvent 保留基础 `character` 用于物理键路由，追加独立文本字符；KeyboardAdapter 根据已安装键盘库的 `value_first/value_second` 生成 Shift 后字符，维持当前按下沿及重复键处理。
- InputRouter 增加默认关闭的文本编辑上下文参数，旧调用行为保持不变。顺序为现有 Fn 全局分派 → Alt 快捷键 → 编辑上下文 → 普通方向映射。
- 编辑上下文仅接受普通字符/Shift 字符、Enter、Tab、Backspace；不将 Shift+字母丢弃，也不将文本符号映射为方向。Ctrl/Opt 组合保持消费。本期不新增 Caps Lock 组合键定义。
- `settings.directionMapping=false` 时使用 Tab/Enter/Backspace 完成设置操作，不强制覆盖用户的输入配置。
- main 的设置输入分支置于 `!session.ready()` 拦截之前；不改变脚本快捷键分派和其他模块离线规则。
- Codex 周期修改沿用现有 `applyConfig` 和任务 updateInterval：变化才重新计时，不恢复自动开关、不清缓存、不取消在途查询。页面显示以服务读取结果和控制器当前周期为准。

## 7. 验证与实现约束

| 验证 | 主要覆盖 |
| --- | --- |
| native 配置测试 | 档位、分钟边界、JSON 尾随垃圾、字段字节/转义上限、普通/企业密码差异、磁盘写/回读失败、模块隔离 |
| native 输入测试 | Shift 字母及符号、数字 1～4 文本、退格/Tab、中文退格、方向映射开关、Fn/Alt 现有优先级 |
| fake Wi-Fi 状态测试 | 用户名认证分流、保存/开机不启用无线、扫描不连接、扫描/测试终态关闭、模块成功保持至显式关闭、无配置拒绝、Busy 互斥、过期请求隔离、重复关闭、使用中断线清理、关闭失败不虚报、结果保留、重复操作、旧连接不能误判成功、切页继续、回绕、扫描去重/容量 |
| 已有回归 | Codex 配置/刷新、输入路由/配置、脚本集成、文件存储；确保旧行为不受影响 |
| 目标构建 | 检查安装库接口与链接、固件资源；不能作为无线兼容性证明 |
| 真机 | 离线进入设置；五档实际亮度与重启恢复；长字段明文编辑；普通/公司网络成功与错误密码超时；企业转普通；扫描/测试期间 BLE/脚本/Codex/授时正常；保存和重启不连接、扫描/测试后关闭且静置不重连；断电恢复 |

新增注释解释企业认证清理、测试不持久化及结束释放无线、模块使用权与过期关闭隔离、物理键与文本字符分离、配置保存/应用分离及超时回绕。避免重复描述函数名，避免新增与本次无关的公共框架。

所有生产修改留到 Phase 4；本阶段只生成规格。本次无构建、测试或真机验证结果，用户对 focus-clock 的成功反馈仅作为参考项目证据。实际目标网络在 ADV 的连接验收仍需执行。

## 8. 参考与待确认

- 主要代码参考：`/Users/naaran/Github/focus-clock/src/wifi_service.cpp`、`src/wifi_logic.cpp`。参考认证、清理、异步处理及测试结束释放连接的行为；采用适合本期的固定按需策略，不移植完整 AP/STA 消费者系统、网页空密码保留或持久化实现。
- 当前安装头文件：`framework-arduinoespressif32/libraries/WiFi/src/WiFiSTA.h`、`WiFiScan.h` 及 ESP32-S3 `esp_wpa2.h`；M5Cardputer `Keyboard.h`；M5GFX `LGFXBase.hpp`。
- [Espressif 企业 Wi-Fi 示例说明](https://github.com/espressif/esp-idf/blob/master/examples/wifi/wifi_enterprise/README.md)用于认证方式背景说明；具体调用以本机安装库和上述参考源码为准。

待确认问题：无。2026-09-06 用户回复“继续”，确认当前设计并进入任务拆解。
