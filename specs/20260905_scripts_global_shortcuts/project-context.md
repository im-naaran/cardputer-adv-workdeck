> 状态：已确认

# 脚本模块与全局快捷键：项目上下文

## 当前实现（2026-09-05 静态核对）

- 固件：PlatformIO、Arduino/C++17、M5Cardputer 1.2.0、ArduinoJson、LittleFS；电脑端：macOS、Python 3.12/3.13、asyncio、Bleak。
- `firmware/src/core/navigation_service.*` 定义四栏：Codex、脚本、剪切板、设置；支持 Fn+1～4 和 Fn+左/右。`application/app_shell.cpp` 为第五栏预留空间，尚无第五模块。
- Codex 已实现；另外三栏使用 `PlaceholderPage`。`main.cpp` 先分发全局导航，再向 Codex 分发页面按键；未连接时拦截页面业务操作。
- `platform/keyboard_adapter.*` 只提供 Fn、1～4、方向和 Enter 事件，缺少 Alt、普通字符和第五栏事件。实际库的修饰键、字符和按下边沿行为需在设计阶段核对已安装头文件。
- Codex 的 Enter 手动刷新；Fn+Enter 开关自动刷新，目前仅在 Codex 页有效。新要求的 Fn 全局语义需要明确迁移规则。
- `desktop/config.json` 已有 actions 数组及 type/actionId/name/key/enabled/content/params 字段；解析器目前仅接受 codex.usage.read，尚未检查快捷键唯一性。
- 电脑端已有 ActionRegistry、ModuleManager、异步请求处理和按整帧串行发送；没有脚本执行器、动作目录同步或剪切板执行能力。
- BLE 使用 JSONL、4096 字节帧上限、capabilities 和 actionId/execId 关联。固件已有 MessageRouter、OutgoingJsonlQueue、ExecIdGenerator 和会话失效清理。
- 当前源码和 README 已有跨页 Codex 调度、电脑授时和 ADV 配置保存入口，不能按早期进度记录假设尚未集成。

## 可复用能力与差距

| 范围 | 复用与补充方向 |
| --- | --- |
| 输入与导航 | 扩展现有事件和全局分发，保留顶部布局；加入修饰键优先级和每模块方向映射 |
| 桌面配置 | 扩展 actions 校验与注册，统一快捷键规范化、冲突诊断和配置顺序 |
| 协议与会话 | 复用现有封包、发送队列和请求关联；补齐目录获取及脚本执行契约 |
| 页面 | 复用显示适配与样式；脚本列表需要选中、滚动和执行状态 |
| 配置 | 复用 ADV 文件读取/保存模式；方向映射设置的具体文件和生效入口留待设计 |
| 后台服务 | 脚本执行不能阻塞 BLE、Codex 或授时；不另建周期轮询框架 |

## 约束与风险

- 当前仅生成本规格目录下的文档，不修改生产代码或依赖，不执行脚本、构建或刷写。
- 第五栏用途、旧 Fn+Enter 行为、剪切板/配置页本次实现范围尚待需求确认。
- 全局快捷键可能在执行中、断线或按键组合变化时再次收到事件，需要避免一次操作重复执行；超时后不能自动重放副作用操作。
- 动作目录大小须适配现有帧上限，具体容量和超限行为在设计阶段落实。
- 自动化可验证路由、配置、目录和执行状态；物理组合键、中文显示、BLE 及浏览器打开由人工验收。
