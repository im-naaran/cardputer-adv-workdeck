# Cardputer ADV Workdeck

Cardputer-Adv 与 macOS 桌面服务通过 BLE 配合的工作台。设备发起请求，电脑读取本机数据并返回结果。

目前支持 Codex 用量查看、跨页面自动刷新、脚本列表与全局快捷执行，以及电脑授时和每小时同步。设置支持离线调整亮度、Wi-Fi 配置与连接测试、Codex 刷新周期；剪贴板支持从电脑配置常用文本并直接粘贴到 macOS 当前输入框。

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

当前协议为 **v2**，需配套更新固件和电脑服务；`configVersion` 仍为 1。粘贴的 macOS 权限与使用边界见 [剪贴板配置与权限](desktop/README.md#剪贴板配置与权限)。

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
| `Fn+1`～`Fn+4` | 切换 Codex / 脚本 / 剪贴板 / 设置 |
| `Fn+,` / `Fn+/`（左 / 右） | 循环切换页面 |
| Codex 页 `Enter` | 手动刷新 |
| Codex 页 `Fn+Enter` | 开关自动刷新 |
| 无修饰 `;` / `.` | Codex 滚动、脚本 / 剪贴板列表选择（上 / 下） |
| 脚本页 `Enter` | 执行选中脚本；加载失败时只重读目录 |
| 剪贴板页 `Enter` | 粘贴选中文本；加载失败时只重读目录 |
| 任意页 `Alt+G` | 默认动作：电脑打开 Google |
| 设置页 `Tab` / `Enter` / `Backspace` | 选择 / 确认 / 返回；编辑时退格删除字符 |

脚本按电脑配置顺序分页展示，每页最多 8 条，不限制脚本总条数。快捷键不依赖当前页面或目录加载；普通 `g` 不执行动作，重复快捷键只匹配首个有效启用动作（脚本和剪贴板共用）。配置修改后重启桌面服务并重连，详见 [脚本配置](desktop/README.md#脚本配置与执行)。

自动刷新默认每 5 分钟一次，由 ADV 的 `/config/codex.json` 控制，范围为 60～3600 秒。切页后仍刷新；关闭后可手动查询，重新开启后等待完整周期。开关在重连后保留，重启恢复默认开启。

连接电脑后自动授时，成功后每小时同步。断线期间继续走时；重启后重新等待授时。授时独立于 Codex 查询，不新增时钟页面。

`Fn+4` 离线进入设置：亮度共五档、默认 60%，调整即生效并保存；Codex 周期在当前页调整 1～60 分钟，停止调节后自动保存。Wi-Fi 的 SSID、用户名和密码均明文显示：用户名为空使用普通网络，非空使用公司 PEAP 网络。

自动息屏默认无键盘操作 10 分钟，可在 `Fn+4 → 自动息屏` 选择“永不 / 1 / 5 / 10 / 30 分钟”，修改即生效并保存；保存失败时 Enter 重试。息屏后任意键只点亮屏幕，整个按键组合释放后，下一次按键才执行操作。长按不会息屏，唤醒恢复原亮度；BLE 和后台任务继续运行，后台消息不自动亮屏。电量启动读取一次，此后统一每 60 秒读取，按键和唤醒只使用缓存。

Wi-Fi 固定 AUTO 按需启用：开机和保存不连接，主动扫描或测试后关闭无线并保留结果。测试成功表示认证完成并取得 IP，不表示公网可用。关闭失败后每隔至少 1 秒自动重试，最多额外 3 次；仍失败时可在网络信息页手动重试。详细步骤见 [设置操作与公共 Wi-Fi 接口](firmware/README.md#设置操作)。

## ADV 模块配置

首次使用运行时配置前，在项目根目录部署 LittleFS 默认文件：

```sh
pio run -d firmware -t uploadfs
```

镜像来自 `firmware/data/`。该命令会覆盖整个文件系统分区；已有自定义文件时先备份。日常调整使用设备设置页或下面的串口周期命令，不重复 uploadfs。亮度和 Wi-Fi 配置文件在已挂载文件系统中首次保存时创建，已有 LittleFS 的设备无需为新增设置重新 uploadfs。普通应用刷写在分区布局不变、未擦除 Flash 的情况下保留配置。

打开 115200 波特率串口监视器后逐行发送（配置命令需要换行）：

```text
codex.config.read
codex.config.save {"refreshIntervalSeconds":60}
codex.config.reload
```

`read` 每次读取最新文件，同时显示当前运行周期；`save` 保存并立即应用，无需重启；`reload` 重新应用文件。周期改变后从生效时刻等待完整新周期，相同值不重设起点。关闭自动时保存不会开启任务，正在等待的查询仍可完成。

文件读取失败时，启动使用默认 300 秒，运行中保留当前周期。`ReloadFailed` 或 `ApplyFailed` 表示尚未成功应用，文件可能已保存，可修复问题后 reload。未部署文件系统时会报告 `NotMounted`，固件不会自动格式化。设备设置页复用同一保存入口。

方向映射可按模块独立保存，例如 `input.config.save {"scripts":{"directionMapping":false}}` 仅关闭脚本页映射，其余缺省为开启。命令同样需要换行，保存后立即生效；详见 [输入配置](firmware/README.md#按键与方向映射)。

## 代码与参考资料

- [desktop](desktop/README.md)：桌面分层、配置、动作注册与并发处理。
- [firmware](firmware/README.md)：固件分层、主循环、调度和平台边界。
- [protocol](protocol/README.md)：共享消息契约、BLE 常量与 fixtures。
- [specs](specs/)：需求、设计和任务历史。
- [授时验收记录](specs/20260905_scheduled_task_time_sync/tasks.md#task-15-操作说明与真机验收)。
- [配置验收记录](specs/20260905_adv_module_runtime_config/tasks.md#task-08-真机配置持久化与即时生效验收)。

- [脚本与快捷键验收](specs/20260905_scripts_global_shortcuts/acceptance.md)：含 17 条脚本配置及待人工确认项目。

- [设置模块真机验收](specs/20260906_settings_module/acceptance.md)：操作步骤、预期与实际结果记录。

- [剪贴板与直接粘贴验收](specs/20260907_clipboard_direct_paste/acceptance.md)：混合配置、分页和真实输入检查。

- [第一期省电实机验收](specs/20260910_firmware_power_saving/acceptance.md)：背光、首键、后台连续性及功耗对照，实机结果待确认。
