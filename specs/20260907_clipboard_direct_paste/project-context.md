> 状态：已确认

# 剪贴板与直接粘贴：项目上下文

日期：2026-09-07。Phase 0 已完成；本状态表示上下文分析完成，不代表需求获批或真机验收完成。

## 当前架构

- 固件：ESP32-S3 / Cardputer ADV，Arduino、C++17、PlatformIO、ArduinoJson，Application / Core / Platform 分层。
- 电脑端：macOS，Python 3.12/3.13、asyncio、Bleak、uv；业务与 OS Adapters 分离。
- BLE 使用 JSONL、固定协议动作、execId、分片发送队列和会话能力声明。单条 JSON 上限 4096 字节，正文不应通过 BLE 传输。
- 基础通信、Codex、脚本和设置已有实现；剪贴板目前为占位页。保持四模块顶栏和 Fn+3 入口。

## 已核对的复用点

| 现有位置 | 能力与本次处理方向 |
| --- | --- |
| `firmware/src/application/scripts/scripts_page.cpp` | 四行列表、圆角选中高亮、长名称裁剪、快捷提示、底部位置计数；作为剪贴板视觉与操作基准 |
| `firmware/src/application/scripts/scripts_controller.*` | 分页、目录失败重读、请求关联和超时；借鉴状态管理，设计阶段决定共享边界 |
| `firmware/src/application/app_shell.cpp` | 顶栏、离线页、底部临时反馈；直接复用 |
| `firmware/src/core/input_router.cpp` | Fn 导航优先、Alt 全局快捷键、模块方向映射；扩展应保留统一入口 |
| `firmware/src/main.cpp` | 页面装配、请求分发、clearModuleSession 清理；加入剪贴板生命周期 |
| `desktop/src/adv_helper/config.py` | 统一动作模型及非法可选动作隔离；当前尚不接受 clipboard 类型 |
| `desktop/src/adv_helper/application/script_module.py` | 当前 actions.list 和全局快捷键仅面向脚本；不能直接假定已经支持所有动作类型 |
| `desktop/src/adv_helper/bootstrap.py` 与 `core/` | 注册、路由、会话请求取消和发送锁；复用通信框架 |
| `desktop/src/adv_helper/os_adapters/` | 当前没有剪贴板写入及模拟粘贴适配器；需新增平台能力 |

## 本次输入与约束

- 用户要求沿用脚本模块风格，由电脑服务直接粘贴到当前焦点输入框。
- 已阅读本地思路草稿；正式需求在同目录 requirements.md 完整表达，不依赖未提交的准备文档。
- 用户已确认沿用全局 Alt+字母；脚本与剪贴板共用一套动作核心，仅 UI 按类型分组，不建设独立的目录和执行调度核心。
- 用户已确认忙时拒绝、不排队；需求按统一核心采用共享执行槽，粘贴覆盖写入到发送粘贴的完整过程。
- 系统差异限定在底层适配能力，用户已确认首批交付 macOS，预留 Windows/Linux。
- 原文本必须保留空格、换行与 Unicode；现有通用配置解析会 strip 字符串，不能原样套用。
- 先完成需求、设计、任务的确认，仅在执行阶段修改生产代码。本轮不安装依赖、不运行构建或测试。

## 验证边界与风险

- 自动化应覆盖文本原样传递、写入后才触发粘贴、并发隔离、部分失败、会话清理及快捷键冲突。
- 系统权限、实际前台输入、焦点切换、中文字体、BLE 与设备画面需要人工验证。
- OS 接受模拟粘贴不等于目标控件确实插入文字；反馈必须区分已发出操作与实际输入验收。
- macOS 适配 API、权限判断及超时清理机制留到设计阶段核实，本阶段不预定具体依赖或系统命令。
