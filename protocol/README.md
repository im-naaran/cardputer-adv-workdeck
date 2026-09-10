# Cardputer ADV Workdeck 共享协议

> 当前两端运行协议 v2，必须配套更新；configVersion 保持 1。`fixtures/v2/` 提供当前动作契约及 v1 拒绝样本，根 fixtures 仅保留两端继续使用的 Codex/授时测试数据。

本目录是 Cardputer-Adv 固件与 macOS 电脑端共同遵循的协议契约。`fixtures/` 中的 JSON 用于跨语言测试；双端实现应从这里复制相同的常量，并用 fixtures 验证编码、解码和兼容行为。

## 协议常量

| 常量 | 值 |
| --- | --- |
| 协议版本 | `2` |
| GATT Service UUID | `5fd5b6a4-60a1-48e1-a4f3-69c9cab741d2` |
| ADV → PC notify Characteristic UUID | `a6c791a8-824d-4f3c-8708-0a05c8287ba3` |
| PC → ADV write Characteristic UUID | `7d913c17-e2dd-4c15-b9c2-d1ba4e372ef3` |
| 单条 JSON 最大字节数 | `4096` |
| BLE 安全分片载荷 | `20` bytes |
| ADV notify 分片间隔 | `10` ms |

这组 UUID 仅属于 Cardputer ADV Workdeck，不复用参考项目的 Nordic UART UUID。设备名称和 macOS CoreBluetooth 设备 UUID 只用于辅助选择，不能替代 GATT Service UUID 进行协议识别。

## 传输格式

线上传输使用 UTF-8 JSON Lines。每条业务消息必须序列化为不含格式化换行的紧凑 JSON，再追加一个 LF 字节（`0x0A`）：

```text
compact UTF-8 JSON | 0x0A
```

`fixtures/*.json` 为方便代码审阅而保留缩进；发送前必须紧凑序列化，不能直接把格式化文件内容作为一条 JSONL 消息发送。接收端应先按 bytes 组装完整行，再统一解码 UTF-8，避免中文字符跨 BLE 分片时损坏。

## 消息外层

请求使用 `event=request`、`actionId`、`execId` 和 `payload`；响应使用 `event=response`、相同的 `actionId`、原样返回的 `execId` 和 `result`。

`execId` 是请求关联标识。业务响应必须原样携带请求的 `execId`；ADV 同时校验 `event=response`、actionId 和当前非空在途 ID，超时或旧会话的响应不得更新状态。

动作标识：

- `system.hello`：BLE 建连后的会话初始化响应。
- `codex.usage.read`：ADV 发起的 Codex 用量查询。
- `system.time.read`：电脑默认提供的被动系统授时能力，与 Codex 是否启用无关。

## 系统时间契约

ADV 仅在 hello 宣告 `system.time.read` 时发送空对象 payload 的请求。成功响应
data 包含 `epochMilliseconds`（非负 int64 UTC Unix 毫秒）和 `utcOffsetMinutes`
（-720～840 的整数偏移），两者对应电脑同一次采样。Unix 时间不预先加偏移。
未知字段忽略；缺字段、布尔、小数、字符串和溢出值无效。设备还须校验当前
平台 `time_t` 可表示范围，设置失败不能标记授时成功。

系统能力沿用协议版本 1；旧桌面没有 capability 时不请求。桌面不建立授时定时器。
`system_time_request.json`、`system_time_success.json` 和 `system_time_invalid.json`
分别用于请求、成功与非法业务字段测试。

## Hello 契约

电脑端在 BLE 建连后主动发送一次 `system.hello`，用于协商版本、电脑身份和能力；后续业务查询由 ADV 发起。

`system.hello` 的 `result.data` 包含：

- `protocolVersion`：当前为 `2`，不兼容时不得进入业务同步。
- `computerId`：电脑端生成的稳定本机标识，不是 BLE MAC。
- `computerName`：供设备展示的名称。
- `capabilities`：当前电脑端支持的业务动作列表。

新 hello 不携带 settings。旧 hello 的 `settings.codexRefreshIntervalSeconds` 作为未知字段忽略，不能覆盖 ADV 配置。协议 v1 的 hello 在当前会话层拒绝；未知 settings 字段不覆盖 ADV 本地配置。

## Codex 用量契约

成功响应的 `result.data` 包含获取时间 `fetchedAtEpochSeconds` 和非空窗口数组 `windows`。每个窗口支持：

- `limitId`：额度桶稳定标识。
- `limitName`：可选展示名，缺失时为 `null`。
- `windowKind`：`primary` 或 `secondary`。
- `usedPercent`：整数百分比；电脑端保留原值用于诊断，固件绘制前裁剪到 `0..100`。
- `windowDurationMins`：可选窗口时长，缺失时为 `null`。
- `resetsAtEpochSeconds`：可选 Unix 重置时间，缺失时为 `null`。

多额度桶按 `limitId` 稳定排序，每个桶按 `primary`、`secondary` 顺序展开；空窗口不产生列表项。接收端忽略未知字段；必需外层字段缺失时解码失败，由会话层丢弃并保留诊断。

稳定结果代码：

| code | 含义 |
| --- | --- |
| `OK` | 查询成功 |
| `NOT_LOGGED_IN` | 本机没有可用的 ChatGPT/Codex 登录 |
| `BUSY` | 已有查询执行中 |
| `CODEX_UNAVAILABLE` | Codex CLI 缺失、无法启动或协议不兼容 |
| `TIMEOUT` | App Server 请求超时；设备本地等待超时也使用该状态 |
| `INVALID_RESPONSE` | App Server 响应无法转换 |
| `ERROR` | 其他可恢复错误 |

## 调度、兼容与失败恢复

ADV 在有效 hello 后按 capability 发起请求，先提交授时、再提交 Codex；重复 hello 不重复首次请求。旧桌面缺少 `system.time.read` 时不发授时请求，原 Codex 动作仍可使用。电脑身份改变后旧在途响应失效，两个动作共享开机级递增 execId，断线不重置序号。

授时成功后以设备单调时间等待一小时；同电脑新鲜重连不推迟截止时间，到期或失败后重连补同步。Codex 在后台按 ADV 本地文件配置的周期刷新，用户关闭自动后仍可手动请求。

设备请求超时包含排队时间：授时 15 秒，Codex 210 秒。授时失败最多再试 2 次、间隔 3 秒；Codex 等后续周期或手动查询。设备有界发送队列最多 4 条，尚未开始的消息排队超过 10 秒可丢弃；已经发送的半帧继续到 LF，断线则清空。两个动作的整条 JSONL 分片不能交错；迟到响应必须经过 event/actionId/execId 匹配才可更新状态。

## Fixtures

根目录的 9 个 JSON 覆盖未改变的 Codex 与授时消息，不代表仍支持 v1。

| 文件 | 场景 |
| --- | --- |
| `codex_usage_request.json` | ADV 查询请求 |
| `codex_usage_success_single_window.json` | 单窗口成功响应 |
| `codex_usage_success_multi_window.json` | 多额度桶、多窗口成功响应 |
| `codex_usage_not_logged_in.json` | 未登录响应 |
| `codex_usage_error.json` | 可恢复错误响应 |
| `codex_usage_unknown_fields.json` | 合法消息携带未来未知字段 |
| `system_time_request.json` | 空 payload 授时请求 |
| `system_time_success.json` | UTC 毫秒与时区偏移 |
| `system_time_invalid.json` | 非法授时字段 |

## 实现位置与维护

- 固件：`firmware/src/core/protocol_constants.h`、`message_codec.*`。
- 桌面：`desktop/src/adv_helper/core/protocol_constants.py`、`messages.py`。
- 更新字段或动作时同步双端实现与共享 fixtures；未知字段保持可忽略，新增能力通过 hello 协商。调度与发送实现细节分别见 [固件说明](../firmware/README.md) 和 [桌面说明](../desktop/README.md)。

以下命令在项目根目录检查 JSON 语法：

```sh
jq empty protocol/fixtures/*.json protocol/fixtures/v2/*.json
```

双端契约用例分别位于 `firmware/test/test_protocol/`、`desktop/tests/test_messages.py` 和 `desktop/tests/test_protocol_constants.py`。JSON 语法检查不替代这些语义断言；首次查询、重连和分片顺序由双端集成用例覆盖。

## v2 统一动作契约

协议版本为 2，配置版本仍为 1；GATT、JSONL、4096 字节上限、20 字节分片不变。
固件和桌面必须配套更新，v1 hello 拒绝进入 v2 业务会话，不降级或重放请求。
`fixtures/v2/hello_v1_rejected.json` 是结构合法但版本不兼容的会话样本。

hello 新增 `supportedActionTypes`：由执行器可用性生成的 script/clipboard 无重复子集，
与目录条数无关；权限不足在执行时报告。动作仅注册 `actions.list`、`actions.execute`、
`actions.shortcut.execute`；移除 `scripts.execute`。Codex 和授时消息不变。

| 动作 | 严格请求 payload | 响应 data |
| --- | --- | --- |
| actions.list | type=script/clipboard、offset | type、offset、total、nextOffset、actions |
| actions.execute | actionId | 统一执行结果 |
| actions.shortcut.execute | 单个小写字母 key | 统一执行结果 |

每种类型独立分页，每页最多 8 项，无条目总数业务上限。分页数值必须为 uint64 整数，
offset 按 8 对齐；空目录仅 offset=0，非空目录 offset<total；非末页恰好 8 项、nextOffset=offset+8，末页 nextOffset=null。先按完整有效启用配置顺序计算
全局 effectiveKey，再按类型分页。重复 key 仅首项生效，不可用首项也不回退执行后项。
目录项含 type、actionId、name、key、effectiveKey，不发送正文、params、enabled。
ID 使用对应的 script./clipboard. 前缀与非空 ASCII 标识符后缀，总长最多 64 字节；
name 最多 64 UTF-8 字节，不含 ASCII 控制字符。元数据允许无害未知字段，不能携带正文。

统一执行结果必含 `type/actionId/name/exitCode/clipboardWritten/pasteSent/reason`。
外层 actionId/execId 回显请求，data.actionId 是实际配置 ID。

- script：exitCode 为 int32 或 null；两个粘贴状态均为 null；OK 必须 exitCode=0。
- clipboard：exitCode=null；两个状态严格为布尔或 null（结果未确认），OK 必须均为 true。
  pasteSent=true 必须以 clipboardWritten=true 为前提；调用超时不得将未知结果写成 false。
- reason 成功为 null，失败类别为 UNAVAILABLE、PERMISSION_DENIED、WRITE_FAILED、
  PASTE_FAILED、UNCONFIRMED、EXECUTION_FAILED；未解析目标或 BUSY 可 data=null。
- 顶层 code 沿用原集合，业务使用 OK/ERROR/BUSY/TIMEOUT；成功只表示系统操作已发送，
  不证明目标控件实际插入。错误不包含原始 stderr 或正文。

脚本与粘贴共用一个执行槽，忙时拒绝、不排队。写入失败不继续粘贴，部分失败返回状态；
超时、断线、重连不自动重放。设备校验外层动作、execId、类型及预期 ID，并清理旧会话。

v2 fixtures 分开提供两类目录、执行与快捷成功、写入/部分失败、超时、BUSY、无匹配、
非法请求/分页/伪成功。`invalid_*.json` 必须拒绝；v1 拒绝样本在会话层检查。
独立契约检查位于 `desktop/tests/test_action_contract.py`，固件 `test_protocol` 同样消费 v2 fixtures。

`v2/` 的 27 个 JSON 均由桌面参数化契约测试读取；其中 26 个消息样本也由固件协议测试读取。
`v2/protocol_constants.json` 由桌面常量一致性测试读取，不是可发送的消息。
`hello_v1_rejected.json` 专门验证拒绝旧版连接，需要保留。
