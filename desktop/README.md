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
| `application/time_module.py` | 单次采样 UTC 毫秒及同一时刻的本地偏移 |
| `os_adapters/` | Codex App Server 子进程与本机稳定身份 |
| `platform/` | BLE 扫描/连接/分片/重连与本地诊断 |

数据流：BLE bytes → `JsonlBuffer` 完整行 → `decode_message` → `ActionRegistry` / `MessageRouter` → handler → 编码 → BLE 分片发送。

`DesktopApplication.run_session()` 建连后先发送 hello，再为请求创建异步任务。Codex 慢查询不会阻塞授时；`_send_lock` 覆盖整条响应发送，避免不同响应分片交错。断线时取消并等待本会话请求任务，清空在途 ID。

## 动作与配置

`system.time.read` 在 `build_application()` 中默认注册，不受可选 Codex 配置控制。桌面不维护刷新或授时周期；hello 只声明版本、身份和能力。新增动作时复用注册表与 handler 接口；capabilities 由实际注册动作生成。

`config.py` 当前只接受版本 1 配置与 `codex.usage.read` 可选动作，示例见 [config.json](config.json)。

| 配置 | 默认值 | 范围与含义 |
| --- | --- | --- |
| `configVersion` | `1` | 必须为 1 |
| `settings.codex.requestTimeoutSeconds` | `15` | 5～60 秒，单次 App Server RPC 等待上限 |
| `actions[].enabled` | `true` | 是否注册 Codex 动作；不影响系统授时 |

旧配置中的 `refreshIntervalSeconds` 仅产生废弃提示，其值不会保存或下发；请改用 ADV 模块文件。RPC 默认值统一由 `codex_app_server.DEFAULT_RPC_TIMEOUT_SECONDS` 提供。

顶层或 settings 非法会拒绝配置；单个可选动作非法会记录错误并跳过。新增可配置动作需同时扩展 `config.py` 校验与 `bootstrap.py` 装配，不能只向 JSON 增加 actionId。

CLI 的默认配置路径由 `main.py` 相对源码定位到 `desktop/config.json`，不依赖当前工作目录。`--config` 可覆盖；`--ble-id` 按 CoreBluetooth UUID 选择设备，`--ble-name` 按精确广播名选择。

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
