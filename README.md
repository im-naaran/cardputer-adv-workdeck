# Cardputer ADV Workdeck

Cardputer-Adv 与 macOS 桌面服务通过 BLE 配合的工作台。设备发起请求，电脑读取本机数据并返回结果。

目前支持 Codex 用量查看、跨页面自动刷新，以及电脑授时和每小时同步。其他模块暂为占位页面。

## 准备环境

- 硬件：Cardputer-Adv、可传输数据的 USB 线；电脑开启蓝牙。
- 桌面端：macOS、uv、Python 3.12 或 3.13。使用 Codex 查询前安装 Codex CLI 并完成登录。
- 固件：PlatformIO；可用 `uv tool install platformio` 安装。

## 构建与刷写

以下命令在项目根目录执行：

```sh
pio run -d firmware
pio run -d firmware -t upload
```

多块串口设备同时连接时，刷写命令加 `--upload-port /dev/cu.usbmodemXXXX` 指定端口。
查看串口输出：

```sh
pio device monitor -d firmware -b 115200
```

## 启动桌面端

```sh
cd desktop
uv sync --locked
uv run adv-workdeck
```

设备开机后服务会扫描并连接，断线后自动重连。首次运行时允许终端或宿主应用访问 macOS 蓝牙；按 `Ctrl+C` 停止服务。

有多台设备时，在 `desktop/` 目录先列出设备，再指定其中一个连接：

```sh
uv run adv-workdeck --list-ble
uv run adv-workdeck --ble-id COREBLUETOOTH-UUID
```

默认配置为仓库内的 `desktop/config.json`，可用 `--config /path/to/config.json` 指定其他文件。`--verbose` 开启详细日志。

## 设备操作

| 按键 | 功能 |
| --- | --- |
| `Fn+1`～`Fn+4`、`Fn+←/→` | 切换页面 |
| Codex 页 `Enter` | 手动刷新 |
| Codex 页 `Fn+Enter` | 开关自动刷新 |
| Codex 页 `↑/↓` | 滚动用量窗口 |

自动刷新默认每 5 分钟一次，由 ADV 的 `/config/codex.json` 控制，范围为 60～3600 秒。切页后仍刷新；关闭后可手动查询，重新开启后等待完整周期。开关在重连后保留，重启恢复默认开启。

连接电脑后自动授时，成功后每小时同步。断线期间继续走时；重启后重新等待授时。授时独立于 Codex 查询，不新增时钟页面。

## ADV 模块配置

首次使用运行时配置前，在项目根目录部署 LittleFS 默认文件：

```sh
pio run -d firmware -t uploadfs
```

镜像来自 `firmware/data/`。该命令会覆盖整个文件系统分区；已有自定义文件时先备份。日常调整周期使用下面的串口保存命令，不重复 uploadfs。普通应用刷写在分区布局不变、未擦除 Flash 的情况下保留配置。

打开 115200 波特率串口监视器后逐行发送（配置命令需要换行）：

```text
codex.config.read
codex.config.save {"refreshIntervalSeconds":60}
codex.config.reload
```

`read` 每次读取最新文件，同时显示当前运行周期；`save` 保存并立即应用，无需重启；`reload` 重新应用文件。周期改变后从生效时刻等待完整新周期，相同值不重设起点。关闭自动时保存不会开启任务，正在等待的查询仍可完成。

文件读取失败时，启动使用默认 300 秒，运行中保留当前周期。`ReloadFailed` 或 `ApplyFailed` 表示尚未成功应用，文件可能已保存，可修复问题后 reload。未部署文件系统时会报告 `NotMounted`，固件不会自动格式化。设备设置页面后续将复用同一保存入口。

## 代码与参考资料

- [desktop](desktop/README.md)：桌面分层、配置、动作注册与并发处理。
- [firmware](firmware/README.md)：固件分层、主循环、调度和平台边界。
- [protocol](protocol/README.md)：共享消息契约、BLE 常量与 fixtures。
- [specs](specs/)：需求、设计和任务历史。
- [授时验收记录](specs/20260905_scheduled_task_time_sync/tasks.md#task-15-操作说明与真机验收)。
- [配置验收记录](specs/20260905_adv_module_runtime_config/tasks.md#task-08-真机配置持久化与即时生效验收)。
