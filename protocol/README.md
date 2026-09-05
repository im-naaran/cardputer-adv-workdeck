# Cardputer ADV Workdeck 共享协议

本目录是 Cardputer-Adv 固件与 macOS 电脑端共同遵循的协议契约。`fixtures/` 中的 JSON 用于跨语言测试；双端实现应从这里复制相同的常量，并用 fixtures 验证编码、解码和兼容行为。

## 协议常量

| 常量 | 值 |
| --- | --- |
| 协议版本 | `1` |
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

- `protocolVersion`：当前为 `1`，不兼容时不得进入业务同步。
- `computerId`：电脑端生成的稳定本机标识，不是 BLE MAC。
- `computerName`：供设备展示的名称。
- `capabilities`：当前电脑端支持的业务动作列表。

新 hello 不携带 settings。旧 hello 的 `settings.codexRefreshIntervalSeconds` 作为未知字段忽略，不能覆盖 ADV 配置。协议版本仍为 1；旧固件在该字段缺失时回退到自身默认周期。

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

| 文件 | 场景 |
| --- | --- |
| `protocol_constants.json` | 双端共享常量清单 |
| `hello_response.json` | 旧 hello，保留 settings 及 Codex-only 能力以验证兼容 |
| `hello_without_settings.json` | 不携带周期配置的 hello |
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
jq empty protocol/fixtures/*.json
```

双端契约用例分别位于 `firmware/test/test_protocol/`、`desktop/tests/test_messages.py` 和 `desktop/tests/test_protocol_constants.py`。JSON 语法检查不替代这些语义断言；首次查询、重连和分片顺序由双端集成用例覆盖。
