# CPU 档位与开机自动 Light-sleep：项目上下文

> 状态：已确认

日期：2026-09-11。此状态表示完成现状整理，不代表硬件验收。

## 范围与依据

用户已确认 CPU 与 Light-sleep 方案，正式基线见[需求](requirements.md)和[设计](design.md)，原讨论稿已删除以避免重复维护。CPU 默认 160 MHz，设置可选 80/160/240 MHz 已实现；初始化完成后自动 Light-sleep、亮屏和息屏均允许空闲睡眠仍为待实现目标，进度见[任务清单](tasks.md)。

工程为 ESP32-S3 / Cardputer ADV，PlatformIO、Arduino、C++17、ArduinoJson、LittleFS；桌面服务通过 BLE v2 提供 Codex 用量及动作。四个业务页面、通信协议和桌面端功能维持原有范围。

## 当前实现与复用点

| 模块 | 当前能力 | 本次处理方向 |
| --- | --- | --- |
| `firmware/src/main.cpp` | 串行处理输入、BLE 队列、控制器 tick、绘制；末尾 delay(10) | 保留串行处理，替换空闲等待 |
| `core/scheduled_task_service.*` | 定时任务注册、开关、周期调整与防回绕 tick | 扩展最近到期等待时间，不另建业务调度器 |
| `application/settings/` | 独立配置、简单值自动保存、行内反馈 | 增加 CPU 入口，复用交互 |
| `platform/config_file_store.*` | 临时文件替换、错误状态 | 复用到独立 power.json |
| `application/power/` | 屏幕状态、首键吞键、电量缓存 | 保留屏幕语义，芯片睡眠独立协调 |
| `platform/keyboard_adapter.*` | 物理键与业务键分别处理 | 补睡眠唤醒及事件通知 |
| `platform/ble_transport.*` | 回调入队，主循环处理及发送 | 入队通知主循环，保留原队列和业务归属 |
| `application/wifi/` | 按请求连接、扫描、测试、关闭重试 | 保留生命周期；活动期间保守禁睡 |

## SDK 与硬件待证事实

工程声明 espressif32@6.7.0。task-01 实际构建解析为 Arduino 2.0.16 / IDF 4.4.7；此前看到的 Arduino 2.0.17 来自另一个已安装包，并非当前工程使用的版本。依赖清单、SDK 配置和产物标识见 [SDK 评估](sdk-evaluation.md)，后续实验须单独记录。

此前本轮检查：默认 CPU 240 MHz，PM 未开启，Tickless Idle 配置头未定义，BLE sleep mode/clock 为 0。TCA8418 库默认中断 GPIO11，当前 CHANGE 中断不是睡眠唤醒配置。RTC 配置为内部 RC，不假设板上具备外部低频晶振。

IDF 4.4.7 文档指出 Bluetooth 仍持有禁止 Light-sleep 的锁。此项为可行性门槛，不能用编译成功替代在线睡眠证明。依据见 design.md 的官方资料。

## 基线与约束

- [第一期验收](../20260910_firmware_power_saving/acceptance.md)仍标注待实机验收；其中历史自动化数字不作为本次验证结果。
- 保存/启动 Wi-Fi 配置不主动连接，关闭失败不能误判为空闲；不破坏 BLE 会话来换取睡眠。
- 不改依赖缓存，不格式化 LittleFS，不执行 uploadfs；用户已确认任务清单，具体迁移与补丁仍遵循设计中的授权边界。
- 当前工作区原有 docs/ 未跟踪；task-01 只更新规格文档并运行现有目标增量构建。
