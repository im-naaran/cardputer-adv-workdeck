# 桌面端代码说明

Python 3.12/3.13、asyncio、Bleak；使用 uv 管理依赖。源码包为 `src/adv_helper`，命令入口为 `adv-workdeck → adv_helper.main:cli`。安装和启动见 [项目 README](../README.md)。

## 结构与职责

| 位置 | 职责 |
| --- | --- |
| `main.py` | CLI 参数、配置加载、退出信号和资源关闭 |
| `bootstrap.py` | 应用装配、hello、会话接收循环与响应发送锁 |
| `config.py` | 配置类型、范围检查及可选动作错误隔离 |
| `core/` | 消息编解码、JSONL 组帧、路由、动作注册与在途 ID |
| `application/codex_module.py` | 用量转换、同动作并发门控与错误映射 |
| `application/actions_module.py` | 分页目录、全量快捷键匹配与共享执行槽 |
| `application/time_module.py` | 单次采样 UTC 毫秒及同一时刻的本地偏移 |
| `os_adapters/` | Codex App Server、脚本子进程与本机稳定身份 |
| `platform/` | BLE 扫描/连接/分片/重连与本地诊断 |

数据流：BLE bytes → `JsonlBuffer` 完整行 → `decode_message` → `ActionRegistry` / `MessageRouter` → handler → 编码 → BLE 分片发送。

`DesktopApplication.run_session()` 建连后先发送 hello，再为请求创建异步任务。Codex 慢查询不会阻塞授时；`_send_lock` 覆盖整条响应发送，避免不同响应分片交错。断线时取消并等待本会话请求任务，清空在途 ID。

## 动作与配置

`system.time.read` 在 `build_application()` 中默认注册，不受可选 Codex 配置控制。桌面不维护刷新或授时周期；hello 只声明版本、身份和能力。新增动作时复用注册表与 handler 接口；capabilities 由实际注册动作生成。

`config.py` 接受版本 1 配置、`codex.usage.read`、`script` 与 `clipboard` 可选动作，示例见 [config.json](config.json)。

| 配置 | 默认值 | 范围与含义 |
| --- | --- | --- |
| `configVersion` | `1` | 必须为 1 |
| `settings.codex.requestTimeoutSeconds` | `15` | 5～60 秒，单次 App Server RPC 等待上限 |
| `actions[].enabled` | `true` | 是否启用该可选动作；不影响系统授时 |

旧配置中的 `refreshIntervalSeconds` 仅产生废弃提示，其值不会保存或下发；请改用 ADV 模块文件。RPC 默认值统一由 `codex_app_server.DEFAULT_RPC_TIMEOUT_SECONDS` 提供。

顶层或 settings 非法会拒绝配置；单个可选动作非法会记录错误并跳过。新增可配置动作需同时扩展 `config.py` 校验与 `bootstrap.py` 装配，不能只向 JSON 增加 actionId。

CLI 的默认配置路径由 `main.py` 相对源码定位到 `desktop/config.json`，不依赖当前工作目录。`--config` 可覆盖；`--ble-id` 按 CoreBluetooth UUID 选择设备，`--ble-name` 按精确广播名选择。

## 脚本配置与执行

默认 [config.json](config.json) 已包含：

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

将动作放入 `actions` 数组，修改后停止并重新启动桌面服务，设备重连后获取新目录。启动和连接只同步目录，不自动执行脚本。设备列表 Enter 和任意页 Alt+字母共用执行入口；普通字母不执行。

| 字段 | 规则 |
| --- | --- |
| `actionId` | `script.` 前缀，后缀非空，总长最多 64 个 ASCII 字符；允许字母、数字、点、下划线和连字符；ID 必须唯一 |
| `name` | 非空单行名称，最多 64 UTF-8 字节；设备长名称裁剪不改变动作 ID |
| `key` | 单个英文字母或 `null`；大小写统一为小写，`null` 仍可从列表执行 |
| `content` | 非空脚本原文，最多 8192 UTF-8 字节，禁止 NUL |
| `params.timeoutSeconds` | 整数 1～30，缺省 15；其他脚本参数不接受 |

脚本总数没有业务上限，每次 `actions.list` 最多返回 8 条；ADV 只缓存当前页。重复 key 合法，按整个有效启用脚本配置顺序匹配首项；后项仍可 Enter 执行，设备只给首项显示快捷提示。禁用和非法项不参与目录、匹配或执行，重复 actionId 与重复 key 不同：重复 ID 条目会报错跳过。

hello 固定声明 `actions.list`、`actions.execute`、`actions.shortcut.execute` 三项能力，不逐条列出脚本 ID。快捷键由电脑在完整配置中匹配，目标不在设备当前页时也能执行；空目录或目录加载失败不妨碍快捷键请求。

电脑使用 `/bin/sh -c <content>` 异步执行，工作目录为所加载配置文件的目录，继承桌面服务环境。输入、输出及错误输出均接 DEVNULL，不支持交互或终端输出展示。设备仅发送配置 ID 或快捷字符，不能覆盖命令和超时。

脚本和剪贴板共用一个忙碌槽，忙时返回 BUSY，不排队；Codex 和授时仍能响应。超时或会话取消会对仍在运行的进程组先 TERM、等待最多 1 秒，再 KILL 并回收；主动脱离进程组的任务及已打开的浏览器无法随此操作回滚。失败日志可用 `--verbose` 查看动作 ID、退出码和错误类别，不输出脚本正文。

| 设备反馈 | 含义 |
| --- | --- |
| 已执行 | 命令退出码为 0；Google 示例只表示系统接受打开请求，网页是否成功加载需人工确认 |
| 执行失败 / 忙碌 / 未绑定快捷键 | 本次失败、执行槽占用或没有匹配；不会自动重放 |
| 结果未确认 | 设备等待 45 秒到期或电脑执行超时；先检查电脑实际状态，再决定是否手动触发 |
| 断线 | 旧目录、请求和反馈失效；重连只重新同步目录，不重放执行 |

多页、重复键与异常场景配置见 [人工验收记录](../specs/20260905_scripts_global_shortcuts/acceptance.md)。

## 外部能力与边界

Codex 复用本机 CLI 登录，通过 App Server 获取账户与额度窗口；认证信息不进入 BLE 消息。`CodexModule` 对重叠查询返回 `BUSY`。设备超时不会远程取消已启动的桌面查询。

电脑身份优先取 macOS 平台 UUID，经项目专用哈希生成 `computerId`；失败时使用系统名与 `uuid.getnode()` 回退。该标识用于设备区分数据来源，不是 BLE 地址。

`TimeModule` 可注入采样函数；UTC 与偏移来自同一次采样，偏移每次重新计算。异常由路由转换为错误响应，不影响其他动作。协议字段、错误码与兼容规则见 [共享协议](../protocol/README.md)。

## 开发验证

在 `desktop/` 目录运行：

```sh
uv run --locked python -m pytest
```

`tests/` 使用假 BLE、假 App Server 和注入时钟覆盖协议、配置、并发、断线及错误恢复；共享 fixtures 位于 `../protocol/fixtures/`。真实 CoreBluetooth 行为与设备显示需按 [授时规格的验收步骤](../specs/20260905_scheduled_task_time_sync/tasks.md#task-15-操作说明与真机验收) 检查。

## 剪贴板配置与权限

首批直接粘贴仅支持 macOS；Windows/Linux 只预留适配接口。两端都需更新到协议 v2，旧版 hello 会被拒绝，设备保持等待连接；配置文件的 `configVersion` 仍为 1。使用根 README 的固件 upload 和桌面启动步骤配套升级，不需要为本功能重新 uploadfs。

默认 config.json 已提供以下动作，无末尾换行，不主动提交：

```json
{"type":"clipboard","actionId":"clipboard.npm_install","name":"npm install","key":"p","enabled":true,"content":"npm install","params":{}}
```

在电脑先聚焦输入框，再在 ADV 使用 Fn+3、上下选择、Enter，或任意页 Alt+P。普通 p 不触发。显示“已发送粘贴”只代表系统调用完成发送，需查看目标控件确认文字实际插入。

content 必须为 1～8192 UTF-8 字节的纯文本，原样保留首尾空格、换行、Tab 和 Unicode；不允许 NUL 或非法 Unicode。正文不经 BLE 传输、不作脚本解释。params 只能为空对象。名称为非空单行、最多 64 UTF-8 字节；ID 使用 clipboard. 前缀，总长最多 64 ASCII 字节。配置修改后重启服务并重连。

脚本与剪贴板共用完整配置顺序和执行槽：重复 key 只归第一个有效启用项，后项仍能从列表执行；先匹配后按类型分页，每类每页 8 条，总条数不限。动作执行中再次触发返回忙碌，不排队；完成后再次按键会再次执行。

macOS 首次调用可能出现系统授权提示。在“系统设置 → 隐私与安全性 → 辅助功能”允许实际启动服务的宿主（例如 Terminal、iTerm 或 IDE）；在“自动化”中允许对应宿主控制 System Events。以系统提示显示的宿主为准。授权后重新聚焦目标输入框并手动再次触发，服务不自动补发，也不主动切换窗口。

写入失败不会继续发送 Command+V；写入成功但粘贴失败显示“粘贴失败，文本已复制”，可在目标处手动粘贴。超时/断线显示结果未确认，先检查目标内容再决定是否重试；已发生的写入或按键不能撤回。清理失败后执行器禁用，需重启服务。日志不输出正文或原始系统错误文本。

粘贴保留新的系统剪贴板，不恢复旧内容；写入失败也可能已清空旧内容。服务不附加 Enter、不抢焦点，目标由操作发生时的前台窗口决定。终端对文本内多行的处理由终端自身决定。其他应用可能修改剪贴板，目标应用也可能拒绝或延迟读取，需人工确认实际输入。

完整可复制混合配置、超过两页的列表和验收表见 [剪贴板验收](../specs/20260907_clipboard_direct_paste/acceptance.md)。
