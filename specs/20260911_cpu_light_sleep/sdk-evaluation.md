# SDK 与键盘接入评估

> 状态：调查完成；实验路线待确认，实机能力未验证

日期：2026-09-11。对应 task-01，追踪 R-03/R-05/R-07/R-08/R-09。

## 结论

当前默认构建未开启 PM、Tickless Idle 或 BLE Modem-sleep，不能启用目标自动 Light-sleep。CPU 固定档位及期限查询可以继续实施。重新构建 SDK 只能解决配置能力，不能据此宣称 BLE 在线睡眠成立；task-07 仍须取得真实驻留和通信证据。

本轮完成工程、已安装 SDK/键盘库检查和现有目标增量构建。没有修改生产代码、依赖或安装缓存，没有烧录、改写设备文件系统或测量功耗。

## 实际构建基线

从仓库根目录执行 `pio run -d firmware -e m5stack-cardputer-adv`，退出码 0，耗时 5.047 秒。此次复用已有产物，仅作依赖解析及增量构建证据，不是全量重编或新增行为验证。

| 项目 | 本次解析结果 |
| --- | --- |
| PlatformIO 平台 | espressif32 6.7.0 |
| 板配置 | esp32-s3-devkitc-1，240 MHz，8 MB Flash |
| Arduino 包 | framework-arduinoespressif32 3.20016.0，即 Arduino 2.0.16 |
| 底层 SDK 头文件 | ESP-IDF 4.4.7 |
| Xtensa ESP32-S3 / RISC-V 工具链 | 8.4.0+2021r2-patch5 |
| esptoolpy | 1.40501.0，即 4.5.1 |
| BLE | ESP32 BLE Arduino 2.0.0，工程使用 BLEDevice / Bluedroid |
| M5Cardputer | 声明 tag 1.2.0；实际提交 2d4fa6646e4e5b47e0af96214b003aa7b15b8d81；库 manifest 自报 1.1.1 |
| M5Unified / M5GFX | 0.2.21 / 0.2.28 |
| ArduinoJson | 7.4.3 |
| RAM / Flash 占用 | 75164 / 2099449 字节 |

安装目录中还存在 Arduino 2.0.17，它不是本工程本次解析的包。版本证据必须同时对应构建输出及被选中包，不能只读取无版本后缀的目录。

产物与配置 SHA-256：

```text
firmware.elf
e0608b0e3b46763fe038725e136a9d3506aa165be0c2180c17e8ca72ff7a2ce8
firmware.bin
4e44241e07a3289cfc1ff8584b6bd3bf34ee23a67f3a1ca0d2b1cd0048872aa7
Arduino 2.0.16 tools/sdk/esp32s3/sdkconfig
a70d5c071e68c07e82fe5e71c3225b0854e1021ecd8a902da20523aa3123b476
```

前两项位于 `firmware/.pio/build/m5stack-cardputer-adv/`，是本轮检查时的已有固件，不能作为未来实验产物标识。

## 配置与 BLE 门槛

检查所选 Arduino 包的 `tools/sdk/esp32s3/sdkconfig` 和 `dio_qspi/include/sdkconfig.h`：

| 配置 | 当前值 | 实验要求 |
| --- | --- | --- |
| CONFIG_PM_ENABLE | 未开启 | 在 SDK 构建时开启 |
| CONFIG_FREERTOS_USE_TICKLESS_IDLE | 未定义 | 在 SDK 构建时开启，并核对生成配置 |
| CONFIG_BT_CTRL_MODEM_SLEEP | 未开启 | 核对控制器支持及所需时钟后配置 |
| CONFIG_BT_CTRL_SLEEP_MODE_EFF / CLOCK_EFF | 0 / 0 | 由真实 Kconfig 生成，不能直接伪造派生值 |
| CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ | 240 | 首轮实验固定 160，禁睡/自动睡眠仅切换一个变量 |
| CONFIG_ESP32S3_RTC_CLK_SRC_INT_RC | 开启 | 不是外部低频晶振的证据，也不等同 BLE sleep clock |

SDK 中的 CPU/TAGMEM 睡眠断电选项不等于 PM 已开启。仅添加应用 `-D` 无法重编预编译的 FreeRTOS、PM 或控制器库。

[IDF 4.4.7 ESP32-S3 电源管理文档](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/power_management.html)要求自动睡眠具备 Tickless Idle，并说明 Bluetooth 在 Modem-sleep 时仍持有 NO_LIGHT_SLEEP 锁。因此当前路线不具备发布依据。未取得控制器源码构建与实机锁变化证据前，不进一步断言所有配置组合都不可行，也不绕过驱动锁。

## 候选构建路线与影响

1. **优先候选：同版本独立混合工程。** 固定 espressif32 6.7.0、Arduino 2.0.16（包 3.20016.0）、ESP-IDF 4.4.7（包 3.40407.0）及上述工具链，在独立实验目录采用 `framework = arduino, espidf`。平台 `platform.py` 已明确将混合工程 IDF 选择到 4.4.7；Arduino CMake 允许 4.4.0～4.4.99。保留现有 BLE 栈，先验证可配置 SDK、锁及控制器时钟，不预先承诺在线睡眠。需要新增 CMake、sdkconfig.defaults、固定依赖及构建记录，可能遇到 Arduino/M5 库组件依赖差异。
2. **备选：同版本 Arduino 底层库重编。** 版本化保存构建配置、脚本与产物摘要，通过独立包使用，禁止覆盖安装缓存。该路线增加 SDK 二进制维护成本，而且不自动解除 BLE 限制，优先级低于混合工程。
3. **暂不选择升级路线。** Arduino 2.0.16 的 CMake 拒绝 IDF 5.x，不能直接使用另一个已安装的新 IDF。[IDF 5.2.1 的 ESP32-S3 文档](https://docs.espressif.com/projects/esp-idf/en/v5.2.1/esp32s3/api-reference/system/power_management.html)也仍列出 Bluetooth 禁睡锁限制；仅升级到 5.2.1 不构成解法。若同版本路线失败，先调查支持目标控制器/时钟的明确版本及兼容 Arduino，再提交升级差异与回退方案。

以上是 task-07 可审阅的候选方案，尚未安装实验 SDK 或创建实验构建。具体 BLE 时钟配置必须结合对应版本 Kconfig、板级时钟条件确认，当前不假设板上有外部 32 kHz 晶振。

## 键盘接入

本地 M5Cardputer 提交中的 `KeyboardReader/TCA8418.cpp`：GPIO11，CHANGE ISR 仅设置 `_isr_flag`；`update()` 无标记就退出，每次只取一个 FIFO 事件，随后尝试清中断并更新按键列表。普通边沿 ISR 不提供 Light-sleep 唤醒，相关 API 见 [IDF GPIO 唤醒文档](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/sleep_modes.html)。

`Keyboard_Class::begin(std::unique_ptr<KeyboardReader>)` 是真实存在的注入入口；但现有 TCA8418 Reader 的芯片对象、中断标记和处理函数均为 private，无 ISR 通知 setter，直接继承不能完成所需扩展。

优先候选是在固定 M5Cardputer 提交上维护最小版本化补丁：保留原 ISR 标记行为，追加可选通知；任务上下文核对 GPIO 电平和 FIFO 可用数；零事件不解码，避免 `0 - 1` 下溢；逐事件更新状态并交给现有 KeyboardAdapter，处理次数有界，余量继续通知。补丁在独立受控副本中应用并校验基线摘要，不修改 `.pio/libdeps` 缓存。

替代方案是通过注入入口使用项目维护的 Reader，但需要维护原映射与 FIFO 实现，重复代码更多；不作为默认选择。无论采用哪种方案，都不能在原 Reader 已注册中断后再次 attachInterrupt 覆盖它。低电平唤醒、电平保持、清中断竞态及 FIFO 溢出恢复仍需实机验证。

现有 KeyboardAdapter 每次 M5Cardputer.update 后读取一个快照。不能改成一次排空所有事件后只读取最终快照，否则短按按下/释放会丢失；补丁需保留逐事件输入和息屏首键门控。

## task-07 实验步骤与回退

1. 在独立目录固定候选依赖、源码提交、生成配置和产物摘要；保留默认工程与当前禁睡固件。先构建并核对真实 PM/Tickless 配置、控制器能力及错误路径，再决定是否烧录。
2. 使用 Cardputer ADV 实板及配套桌面主机，记录板修订、供电方式、电池状态、亮度、主机 BLE 版本和信号条件。以固定 160 MHz 比较禁睡与自动睡眠，分别覆盖亮屏静止、息屏、广播、连接空闲、持续往返和重连。
3. 用能覆盖短睡眠间隙的电流仪/采样设备记录测量点、采样率、时长、均值/峰值与能耗。同步使用可信 SDK 驻留指标或硬件时序证据；任务等待时间及 API 返回值不能代替驻留。USB 调试有无连接分开测量，避免测量输出改变睡眠行为。
4. BLE 保持发现、hello、Codex 响应/超时及重连，后续整机增加 Actions 与粘贴；不通过断开 BLE 获取睡眠。任一状态没有睡眠证据或业务异常，task-07 保持未完成，记录具体锁/错误再调查。
5. 键盘单独检查睡前 GPIO11 已低、快速按下释放、长按、修饰键、积压和溢出；核对亮屏首键执行、息屏首键只唤醒、全部释放后下一键正常。
6. 回退先恢复同频率禁睡以隔离睡眠影响，再恢复 240 MHz；实验失败时使用保存的默认固件和构建配置。只更新程序分区，保留 LittleFS，禁止 uploadfs/格式化。烧录前须再次确认产物、分区和设备归属。

## 本轮验证与后续

实际完成：依赖/头文件/配置/调用链交叉检查、现有目标增量构建退出码 0、记录产物摘要。未运行新增单元测试；task-01 无生产逻辑变更。没有设备测量、BLE 在线睡眠、键盘唤醒或回退实机结果。

task-01 调查完成，task-02～task-06 可继续。task-07 需确认具体实验路线及必要补丁，并取得设备证据；task-08～task-12 尚不可执行。完整 CPU 与 Light-sleep 功能仍未完成。
