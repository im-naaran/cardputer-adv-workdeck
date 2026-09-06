> 状态：已确认

# 脚本模块与全局快捷键：技术设计

## 1. 采用方案

保持四栏和现有分层，新增脚本控制器、列表页及电脑端异步执行模块。统一输入分发管理 Fn、Alt 和每模块方向映射；Fn+Enter 是明确保留的 Codex 页内特例。使用已有 actions 数组及 JSONL 请求/响应，不新增桌面热键监听或通用插件框架。

需求追踪：输入对应 R-03～R-06；配置、目录、执行和页面对应 R-01、R-02、R-04；会话及异常处理对应 R-07。

## 2. 电脑端配置与匹配

保留 configVersion=1 和 Codex 原有条目，在默认 actions 末尾追加：

```json
{
  "type": "script",
  "actionId": "script.google.open",
  "name": "打开 Google",
  "key": "g",
  "enabled": true,
  "content": "open https://google.com",
  "params": {"timeoutSeconds": 15}
}
```

- script 的 actionId 使用 `script.` 前缀并在配置中唯一，后缀由用户命名；保留系统目录动作的命名空间。
- key 为 null 或单个 ASCII 字母，统一为小写；空串、多字符及其他字符判为条目错误。普通配置建议不重复，但重复 key 合法，不删除后续条目。
- actionId 重复仍沿用现有首条有效条目保留规则，与重复 key 区分。禁用项不展示、不匹配、不可执行。
- name 为非空单行文本，UTF-8 最多 64 字节；script actionId 最多 64 个 ASCII 字符，限定字母、数字、点、下划线、连字符。content 非空、不得含 NUL，最大 8192 UTF-8 字节，保留脚本原文，不按普通字符串字段裁剪内容。
- params 首版只接受 timeoutSeconds，整数 1～30，缺省 15；未知脚本参数作为该条目错误，不影响其他有效动作。
- 电脑端和设备目录保持 actions 的原始相对顺序。电脑端 Alt 匹配顺序扫描全部有效启用动作，找到第一个 key 相同的条目即停止，不依赖设备缓存；Enter 按选中的 actionId 执行，重复键条目仍可单独选中。
- 脚本配置变化通过重启桌面服务、重连同步。当前只接受 codex 和 script 类型；未来剪切板动作须沿同一配置顺序扩展目录，不能另建按模块优先的快捷键表。

## 3. 协议与目录

### 3.1 能力与消息

沿用协议版本 1、4096 字节上限及现有结果码。hello capabilities 仅增加三个固定能力 `actions.list`、`scripts.execute`、`actions.shortcut.execute`，不逐条列出脚本 ID，避免脚本数量使 hello 超限。空配置仍提供能力并返回空列表。

| 请求 actionId | payload | OK 的 result.data |
| --- | --- | --- |
| `actions.list` | `{"offset":0}` | `{"offset":0,"total":25,"nextOffset":8,"actions":[...]}` |
| `scripts.execute` | `{"actionId":"script.google.open"}` | `{"actionId":"script.google.open","name":"打开 Google","exitCode":0}` |
| `actions.shortcut.execute` | `{"key":"g"}` | 同执行成功数据，标明实际匹配动作 |

列表项为 `{"type":"script","actionId":"script.google.open","name":"打开 Google","key":"g","effectiveKey":"g"}`。重复 key 保留原值，只有全量配置中首个有效启用匹配项的 effectiveKey 为该字符，其余为 null。设备只用 effectiveKey 展示快捷提示，不参与快捷键匹配。

电脑端两个执行入口解析 ID/字符后调用同一执行函数和忙碌槽。只允许规定的 payload 字段；无效、禁用、不存在的 ID 或未匹配字符返回 ERROR，未匹配提示“未绑定快捷键”，不启动进程。响应外层 actionId 始终回显请求的固定协议动作，实际脚本 ID 在 data 中表达。已匹配动作的非零退出返回 ERROR 与 actionId/name/exitCode；已匹配后启动失败、超时返回对应 ERROR/TIMEOUT 与 actionId/name（exitCode 可为 null）；未匹配或非法请求 data 为 null。客户端不依赖 msg 文本分支。

目录不发送 content、params、电脑路径或输出文本；执行始终以电脑本机配置为准，不允许设备覆盖命令或超时。

### 3.2 分页和校验

- 不设置配置脚本总条数上限，不截断接纳列表；数量只受实际电脑资源约束。分页大小和设备缓存是单次传输限制，不是功能条数限制。
- 首版每页最多 8 条，按有效启用脚本顺序切片；offset 为非负整数，必须是 8 的倍数，bool、负数、非整数拒绝。total 为总条数，nextOffset 为下一页 offset，末页为 null；空配置 offset=0 返回空页。非空配置 offset>=total 返回 ERROR。
- 固定配置在桌面服务运行期间保持不变；重启服务必须关闭旧会话。分页只用于该会话快照，不引入热更新版本协调。
- 在发送前计算紧凑 UTF-8 整帧大小（包括外层及转义），必须不超过 4096 字节。现有每项字段上限加 8 条分页应通过最坏转义测试；超限返回小型 ERROR，不丢弃末尾脚本、不静默截断内容。execId 仍受现有长度约束。
- 固件仅缓存当前一页（最多 8 条）及一个有界接收暂存，记录 total、offset、nextOffset 和选中索引；不累积已浏览页面。
- 严格校验必需字段、类型、每页数量、ID/名称字节上限、页内 ID 唯一、key/effectiveKey、offset 与当前请求一致；非末页恰有 8 项且 nextOffset=offset+8，末页条目数与 total 一致。total 在同一会话各次读取中必须一致。整数转换先检查目标类型范围，未知字段忽略。
- 新页完整校验成功后原子替换缓存，非法响应不展示部分新页。失败保持上一页，不能把旧页条目当作目标页条目执行。

### 3.3 获取与恢复

有效 hello 后按现有授时、Codex 请求顺序追加第一页请求；Alt 执行仅依赖 `actions.shortcut.execute` 能力，不等待任何页面下载，目录失败时也可正常执行。

- 上/下在当前页内移动；跨页边界时请求上一/下一页，加载后分别选中末项/首项。全表首尾停止，不循环。加载期间不累计方向输入，Enter 不执行旧页选中项。
- 目录一次请求在途，等待 15 秒（含排队）；失败不做周期轮询，显示加载失败。在失败状态 Enter 重试原目标页，成功后恢复选中规则，再次 Enter 才执行。
- 同一已就绪会话重复 hello 不重复加载。切栏保留当前页与选中项；断线、身份变化、无效重新协商时清空缓存、分页目标和请求关联，重连重新加载第一页。
- 旧电脑不支持目录或执行时显示对应不可用，不影响 Codex/授时。三个能力分别检查；目录可用不代表可以执行。
- 回包必须匹配 event、协议 actionId、execId 和会话有效性；列表执行还须核对返回的实际脚本 ID，快捷键执行在电脑端匹配后以回包名称反馈，不猜测目标。旧请求响应不修改新状态。

## 4. 电脑端执行

新增 `application/script_module.py` 管理目录及共享忙碌状态，`os_adapters/script_runner.py` 封装真实执行；沿用 ActionRegistry/ModuleManager 仅注册三个固定协议动作；全部有效脚本保存在模块本机集合中供按 ID 和快捷键查询。

- 使用异步子进程调用 `/bin/sh -c <content>`，工作目录为配置文件所在目录（由配置加载/bootstrap 传入），环境继承桌面服务；stdin/stdout/stderr 接 DEVNULL，首版不做交互及输出展示。
- 启动独立进程组；非零退出返回 ERROR 和 exitCode，启动失败返回 ERROR。达到配置超时返回 TIMEOUT，并终止进程组、等待回收；先 TERM，最多等待 1 秒，再 KILL 并回收。脚本主动脱离进程组的任务及已打开的浏览器不属于可回滚执行。
- 所有脚本共享一个执行槽。检查并占用发生在首次 await 前；另一个脚本或重复请求返回 BUSY，不排队。目录查询、Codex 和授时不使用该槽。
- 会话取消同样清理尚在运行的进程组，清理完成前不释放执行槽。清理等待设置上限，不能阻塞主事件循环。
- 不重试执行。沿用已有会话在途 execId 拦截；这不是跨断线的“恰好一次”保证。设备不会自动重发；用户重新按键属于新的显式请求，前次结果未知时应先检查电脑状态。
- 默认 Google 动作 exitCode=0 显示“已执行”，不宣称网页加载成功。日志只记录 actionId、execId、耗时、退出码和错误类型，不输出命令正文或环境。

## 5. 固件输入分发

### 5.1 物理事件

核对本地 `firmware/.pio/libdeps/.../M5Cardputer/src/utility/Keyboard/Keyboard.h/.cpp`：KeysState 支持 fn/alt/ctrl/shift/opt；Fn 分支提前返回，Enter 的 Fn 层为 KEY_NONE，因此 state.enter 无法表达 Fn+Enter；isChange 仅比较键数量。

KeyboardAdapter 每轮在 M5Cardputer.update 后读取 keyList 和 getKeyValue 的第一层物理键值，归一化为与硬件库无关的快照；由可 native 测试的按下边沿逻辑产生 KeyEvent。不以 isChange 作为扫描条件。

KeyEvent 在现有 key、fn 后增加字符和 alt/ctrl/shift/opt 字段，保留现有简单构造兼容。普通数字、字母、标点及 Enter 保持物理身份，Fn+标点由分发层解释，不能先把 Fn+Enter 丢掉。

- 只有主键从松开到按下才产生事件，记录该时刻修饰键；先按修饰键再按主键或同次扫描组合有效。
- 主键按住时再按/松开修饰键不产生新的事件；释放并重新按主键才能再次触发。长按不重复，方向键首版也不连发。
- 一次扫描若同时新增多个业务主键，全部忽略该次业务输入，待释放重按，避免硬件枚举顺序决定执行哪个脚本。
- Caps Lock 不改变物理字母匹配；Shift/Ctrl/Opt 附加组合不匹配本次快捷键，也不产生默认标点映射。

### 5.2 路由表

新增小型纯逻辑 InputRouter，输出导航、Codex 自动开关、全局字符动作、页面方向、页面确认或已消费/无动作。它不访问 BLE、文件或屏幕。

| 条件（依次判断） | 行为 |
| --- | --- |
| 含 Fn | 优先处理以下 Fn 分支，随后结束，不继续进入 Alt/页面普通键 |
| Fn+1～4，允许同时含 Alt，但无 Ctrl/Shift/Opt | 全局切四栏；Fn+Alt+数字仍由 Fn 优先 |
| Fn+`,` / `/`，同上修饰条件 | 全局上一栏/下一栏，循环四栏 |
| 仅 Fn+Enter，当前 Codex 页 | Codex 页内开关自动刷新，沿用其连接/状态约束 |
| 其他 Fn（含 Fn+5、Fn+Alt+Enter） | 消费且不执行 |
| 仅 Alt+字母 | 发送字符到电脑端全量首项匹配；不依赖列表缓存 |
| 其他含 Alt/Ctrl/Shift/Opt | 消费且不执行 |
| 无修饰 `; , . /` 且当前模块启用映射 | 转成上/左/下/右页面动作 |
| 无修饰 Enter | 当前页面确认；Codex 手动查询 |
| 其他无修饰键 | 普通页面事件，当前所有页面均不以字母执行脚本 |

独立方向事件仍交当前页面；本机物理上/下标记位于 Fn 层，但未知 Fn+上/下按系统保留处理，默认使用不带 Fn 的标点滚动。关闭标点映射表示主动关闭这条页面导航路径。

## 6. 模块方向映射配置

新增 ADV `/config/input.json`，由输入层读取，内容按模块分别设置：

```json
{
  "codex": {"directionMapping": true},
  "scripts": {"directionMapping": true},
  "clipboard": {"directionMapping": true},
  "settings": {"directionMapping": true}
}
```

这是每模块独立开关的输入配置文件，不增加全局总开关，也不修改 Codex 刷新周期文件。新增 InputConfigService 复用 ConfigFileStore 和 512 字节上限。

- 缺省模块/字段取 true；显式值必须为 boolean，未知模块/字段或无效 JSON 拒绝。启动读取失败使用默认值；运行读取/应用失败保留上一份有效值。
- 提供 `input.config.read`、`input.config.save <完整配置 JSON>`、`input.config.reload`；save 为替换语义，省略项恢复默认，成功后立即重读应用，重启保留。
- 相同配置不写盘；读取或普通按键不写盘；挂载失败不格式化；复用临时文件替换和现有失败状态语义。
- 当前 CodexConfigCommands 独占串口字节流，不能再增加一个抢读串口的 poll。将行读取提取到单一 ConfigCommandDispatcher，按命令前缀交 Codex 或 Input 执行，保留现有 `t` 诊断和 codex.config.* 行为、长度限制与每轮读取预算。
- 本次不增加配置 UI；默认文件随 firmware/data 部署，README 写明串口修改入口及持久化验收。

## 7. 固件状态与界面

ScriptsController 持有当前分页缓存、分页元数据、目录请求 ID、执行请求 ID/协议动作/可选目标脚本 ID/起始时刻；脚本列表页持有选中索引和滚动偏移。两个请求分别关联，不使用 Codex 的在途字段。

- 目录状态：不支持、加载中、就绪（可为空）、加载失败。
- 执行状态：空闲、执行中、成功、失败、结果未确认。设备等待 45 秒，包含排队、电脑最多 30 秒执行及清理/回包余量；队列提交失败立即提示未发送，不置执行中。
- Enter 调用 `executeById(actionId)`，Alt 调用 `executeByKey(key)`，两者复用提交/关联/结果处理逻辑及同一个设备在途执行槽；设备忙时也立即提示，不提交第二条执行请求。
- 超时取消尚未发送的请求；已发请求不能撤销电脑副作用，提示“结果未确认”，不自动重试。迟到结果不覆盖后续执行。
- 切栏不取消目录或执行请求；断线清空当前目录和关联，并结束旧反馈。
- 顶栏维持现有四个图标。脚本列表内容区显示中文名称和实际可用的 `Alt+G`；按电脑返回的 effectiveKey 仅为全表首个匹配项显示该快捷提示，后续条目仍保存原 key，选择 Enter 可执行。
- 上/下越界停止，空列表无选中项；长名称按 UTF-8 字符边界裁剪显示，不改变 actionId 或执行对象。
- AppShell 内容区底部提供简短状态条，3 秒后消退；执行中可显示持续状态，成功/失败显示结果。切栏仍可看到新结果，不占用顶栏电量和连接区域。状态到期使用本地单调时间判断，不注册跨页周期任务。
- 断线仍沿用统一等待页和导航能力，不使用旧目录离线执行。剪切板、配置继续占位，但 Alt 全局动作可用。

## 8. 公共能力复用与影响面

| 检索位置 | 结论及处理 |
| --- | --- |
| keyboard_adapter、navigation_service、main | 扩展事件，新增纯逻辑 InputRouter；导航仍复用 NavigationService，不在各页复制组合键判断 |
| CodexController::onKey | 保留页内 Fn+Enter 与 Enter；由明确路由调用，不提升自动开关为全局动作 |
| message_codec、message_router、connection_session | 扩展分页目录数据和三种固定请求编码，复用路由/会话；保持 Codex/时间解码兼容 |
| OutgoingJsonlQueue、ExecIdGenerator | 复用发送、取消和 ID；不新增传输队列/生成器 |
| desktop config、registry、bootstrap | 扩展脚本校验和注册，保持条目错误隔离；目录/ID执行/快捷键执行共用全部有效启用动作，hello 只宣告固定能力 |
| ConfigFileStore、CodexConfigCommands | 复用存储；只提取共用串口读行与分发，业务配置校验各自保留 |
| DisplayAdapter、AppShell、CodexPage | 复用字体、颜色和绘制；新增脚本列表及最小反馈，不建设通用列表组件库 |
| ScheduledTaskService | 本功能无周期业务任务，不改调度职责；继续验证 Codex/授时并发 |

新增抽象仅限可独立测试的输入路由、边沿识别和确需共用的串口分发；脚本独有状态留在脚本模块。注释解释 Fn 特例、首项匹配、会话失效、执行副作用和进程清理，避免逐行复述。

## 9. 验证与风险

- native：完整快照序列覆盖 Fn+Enter、Fn+Alt、同键数替换、主键长按/释放及修饰键变化；覆盖每模块映射、列表边界、分页状态、目录非法/超限、请求关联、断线/迟到结果。
- Python：配置重复 key 可接受、ID 冲突、全量顺序、超过 16/100 条、多页遍历及帧大小；假执行器覆盖成功、启动失败、非零、超时、忙碌和会话取消；用无害短命子进程验证真实执行器的超时与回收，不在自动化测试中打开浏览器。
- 双端 fixtures：新增多页目录、跨页重复 key、空目录、执行成功/错误及旧 hello 兼容样例，验证真实序列化字节上限。
- 配置测试：缺省、独立开关、非法保存、相同值不写、失败保留、串口半行/超长行、Codex 命令及 t 诊断回归。
- 真机：四页 Alt+G、脚本列表 Enter、单独 g 无动作；Codex Fn+Enter 有效且其他页无效；Fn 切栏优先、中文裁剪、串口保存后重启、BLE 断线和浏览器打开。
- 物理扫描与组合顺序、BLE 吞吐及浏览器实际效果均须人工确认；执行超时和断线无法撤销已发生的外部副作用。此阶段仅设计，不运行构建或执行脚本。

## 待确认问题

无。用户确认取消固定条数上限并继续；技术方案已据此改为分页列表、固定能力宣告和电脑端全量快捷键匹配，其余设计维持。进入任务拆解，尚未授权执行阶段。
