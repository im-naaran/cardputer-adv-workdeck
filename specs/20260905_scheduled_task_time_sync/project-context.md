> 状态：已确认

# 定时任务与系统授时：项目上下文

## 来源与检查结论

以本地前期授时方案（2026-09-04，不随仓库提交）为输入，2026-09-05 对照当前源码检查。统一调度与日历时间分离的方向可行；以下边界需要在本规格中明确，原讨论文档保持原样。

- 授时章节同时描述 hello 立即同步和重连仅未授时/到期同步。采用首次同步立即发起、同一电脑重连按最近成功授时判断；切换电脑重新同步时间与时区。
- 通用调度器的触发基准与最近成功授时不是同一个状态。断线期间跳过回调不能使授时被误认为仍未到期。
- 当前 Codex 无设备侧请求超时；响应丢失会永久保留在途状态，必须补上有限等待和恢复能力。
- 当前 `BleTransport::sendJsonl()` 每 20 bytes 调用一次 `delay(10)`；不能直接作为快速调度回调，设计阶段需确定有界入队与主循环发送方式。
- 现有页面相对时间/电量有每分钟重绘兜底。按原方案的统一调度边界，该长期周期也需归入统一任务；实际绘制继续由主循环按脏标记执行。
- 时区偏移是电脑响应时刻的快照，不包含完整夏令时规则；Unix 时间必须保持 UTC。

## 技术栈与架构

- 固件：C++17、PlatformIO、Arduino、ESP32-S3，`espressif32@6.7.0`、M5Cardputer 1.2.0、ArduinoJson 7；native + Unity 测试。
- 桌面：Python >=3.12,<3.14、uv、asyncio、Bleak；pytest / pytest-asyncio。
- 分层：application 负责控制器与页面，core 负责会话/路由/协议，platform 或 os_adapters 负责硬件和系统访问。
- `firmware/src/main.cpp` 主循环串行处理 BLE、消息、键盘、控制器和绘制；BLE 回调只放入接收队列。
- 协议版本 1，UTF-8 JSONL、4096 bytes 上限、20 bytes 分片；桌面 hello 宣告 capabilities。

## 相关模块与公共能力

| 检索位置 | 已有能力 | 本次缺口与复用方向 |
| --- | --- | --- |
| `firmware/src/platform/monotonic_clock.*` | `MonotonicClock`、无符号 `elapsedMs` | 复用；补充调度周期范围和回绕约束 |
| `firmware/src/application/codex/refresh_policy.*` | 前台页面周期、hello 周期限制在 1～86400 秒 | 周期逻辑迁移至全局调度服务，保留配置边界 |
| `firmware/src/application/codex/codex_controller.*` | 手动刷新、发送、模块内序号 | 扩展后台刷新、启停、超时，序号迁出共享 |
| `firmware/src/application/codex/codex_usage_state.*` | 在途 ID、缓存、错误、响应关联 | 复用；响应需校验 event，超时释放在途；断线仍清空用量缓存 |
| `firmware/src/core/connection_session.*` | hello、capabilities、电脑身份 | 复用；当前 main 未用 supports 限制 Codex 请求，需接入能力门控 |
| `firmware/src/core/message_codec.*`、`message_router.*` | 消息解析及按 action 分发 | 扩展授时数据校验与请求编码，不另建协议栈 |
| `firmware/src/platform/keyboard_adapter.*`、`core/navigation_service.*` | Enter 的 fn 标志、全局 Fn 导航 | `Fn+Enter` 可作为自动刷新开关，保留全局导航优先级 |
| `firmware/src/application/codex/codex_page.*` | 中文页脚、刷新提示、错误提示 | 增加实际周期/关闭状态，验证 240×135 屏幕布局 |
| `firmware/src/main.cpp` | 每分钟电量/相对时间重绘 | 迁移周期标记到统一服务，不把绘制放入调度回调 |
| `desktop/src/adv_helper/core/registry.py`、`router.py` | 异步动作注册和错误隔离 | 复用以注册默认系统授时能力 |
| `desktop/src/adv_helper/bootstrap.py` | 并发请求任务、完整响应发送锁 | 复用；授时不等待 Codex 查询完成 |
| 双端 BLE transport、JSONL buffer | 分片、接收队列、断线清理 | 固件缺少非阻塞发送调度；保留完整消息串行和有界队列 |

未发现现成全局调度、共享 ExecIdGenerator 或系统授时服务。新建这些能力有跨模块复用目的；不为页面单次判断另建框架。

## 时间精度与兼容风险

原方案引用的 [ESP-IDF v5.2 ESP32-S3 System Time](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32s3/api-reference/system/system_time.html) 描述非睡眠高分辨率计时器偏差小于 ±10 ppm，换算每小时约 36 ms；这不是 BLE 端到端授时精度承诺。0.1～0.5 秒仅为待测估计。

该文档版本不能直接代表当前 PlatformIO 所带框架；设计阶段核对实际框架的 `time_t` 范围与时间 API，范围外输入必须拒绝，禁止截断溢出或为此默认升级依赖。

## 约束与验证边界

- 本阶段仅新建本规格目录；仓库现有大量 staged 文件属于用户，保留其内容和暂存状态。
- 不新增桌面定时器，不接入 NTP/Wi-Fi、NVS、睡眠或 TOTP 功能。
- 自动化可覆盖调度、协议和状态机；实际 BLE 延迟、组合键、显示、小时漂移和断电行为必须真机验收。
- 已有第一期规格记载的是历史启动状态，本文件以当前源码为准。
