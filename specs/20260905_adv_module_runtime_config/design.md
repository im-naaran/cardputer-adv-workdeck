> 状态：已确认

# ADV 模块运行时配置：技术设计

## 1. 存储与文件归属

采用片内 LittleFS，运行时文件为 `/config/codex.json`，内容为 `{"refreshIntervalSeconds":300}`。Codex 负责字段和默认值，底层文件读写可供其他模块复用；不依赖 SD 卡。

本地核对依据：PlatformIO 的 `esp32-s3-devkitc-1.json` 指定 `default_8MB.csv`，其 `spiffs` 数据分区位于 `0x670000`、大小 `0x180000`。已安装 Arduino 框架的 LittleFS 接口默认使用 `spiffs` 分区标签；平台 builder 支持 `board_build.filesystem = littlefs`。分区标签无需更名，不新增或调整分区表；实施时以当前工程最终构建解析结果再次核对。

文件布局：

| 位置 | 用途 |
| --- | --- |
| `firmware/data/config/codex.json` | 源码中的默认文件，同时是文件系统镜像输入 |
| ADV LittleFS `/config/codex.json` | 实际运行配置，读取入口每次打开读取 |
| `firmware/src/application/codex/codex_config.*` | 类型、默认值、解析/序列化及模块配置服务 |
| `firmware/src/platform/config_file_store.*` | LittleFS 挂载及有界文件操作；native 注入 fake |

固件默认值集中在 `CodexConfig`，周期合法范围 60～3600 秒。镜像样例必须有一个实体值，用测试断言它与默认常量一致；页面、协议和会话不再各自定义 300 秒回退值。

首次部署使用 `pio run -d firmware -t uploadfs` 安装 `firmware/data/` 镜像，正常固件刷写仍用 `pio run -d firmware -t upload`。`uploadfs` 会覆盖整个文件系统分区，不能当作日常单文件保存；以后有其他模块文件时须先备份。普通应用刷写在分区布局不变且未执行 erase/uploadfs 时保留文件，真机验证此行为。

启动只调用 `LittleFS.begin(false)`，禁止挂载失败自动格式化。未挂载、文件不存在或内容非法时，以默认周期启动并输出诊断；不在普通读取时偷偷创建或覆盖文件。挂载成功且仅文件缺失时，显式保存允许创建 `/config/` 和目标文件。

## 2. 最小接口与调用链

```cpp
struct CodexConfig {
  uint32_t refreshIntervalSeconds = 300;
};

// 模块服务，不是通用配置中心。
ConfigResult read();                       // 每次读取文件，返回校验后的值或错误
ConfigResult reload();                     // read 成功后应用到控制器
ConfigResult save(const std::string& json); // 校验、保存、重读、应用

// CodexController
bool applyConfig(const CodexConfig&, uint32_t nowMs);
void onSessionReady(bool supported, uint32_t nowMs);
```

`CodexConfigService` 依赖文件存储接口、CodexController 和可注入的单调取时函数。控制器只依赖配置类型，不反向依赖服务；不引入注册总线或订阅框架。

`ConfigResult` 包含状态、成功时的配置值及 `changed` 标记。错误区分 `NotMounted`、`NotFound`、`ReadFailed`、`InvalidConfig`、`WriteFailed`、`ReloadFailed`、`ApplyFailed`；保存成功但应用失败不能返回完整成功。主循环据此输出诊断，`changed` 时设置 redrawRequested。未来 settings 调用同一服务入口，不直接写文件或修改 scheduler。

文件存储接口仅提供有界读取和安全替换，返回明确错误；不依赖 Codex 字段，不负责调度。

启动顺序：挂载 → 注册授时/Codex/显示任务（Codex 使用唯一默认值）→ reload Codex 配置 → 进入现有主循环。启动读取失败只保留默认；运行中读取失败保留当前有效周期。

立即应用链路：

```text
串口配置入口 / 未来 settings
  → CodexConfigService.save(json)
  → 校验 → 写入或跳过相同内容 → 重新读文件
  → 获取新的 millis() → CodexController.applyConfig
  → 返回结果，必要时置脏
  → 重新取循环时间 → 控制器 tick → scheduler.tick
```

文件 I/O 完成后才采样应用时间；主循环随后再取 now，避免文件操作后的新时间与循环旧快照发生无符号下溢。

`applyConfig` 校验后换算为毫秒，比较当前任务周期：不同才调用 `updateInterval`，相同直接成功。更新起点为应用时刻，不隐式立即查询、不改变 enabled、不取消在途请求或缓存。页脚必须传入真实任务快照，移除默认 300000 ms 参数。

读取入口不缓存文件内容。调度器保存最近应用的周期是必要运行状态；绘制、周期 tick 和请求发送不访问文件。绕过保存入口的修改通过 reload 或重启应用。

## 3. 校验与保存语义

- 文件和输入 JSON 最多 512 bytes；读之前检查大小，读期间也限制累计大小，拒绝目录及短读/读取失败。
- 根必须是对象；只允许必需字段 `refreshIntervalSeconds`，整数 60～3600，拒绝布尔、小数、字符串、未知字段及尾部非空白内容。首版不引入配置版本和迁移框架。
- 保存先解析输入，再读取现有文件；现有文件合法且值相同则跳过写入，但仍重新读取并尝试应用，因此能修复“文件已改、运行状态未更新”。
- 读取现有文件为 NotFound 或 InvalidConfig 时，允许用户显式保存修复；NotMounted/ReadFailed 时返回错误，不盲目覆盖。
- 真正写入时先写同目录 `/config/codex.json.tmp`，核对写入字节数、关闭后重读并校验，最后通过 rename 替换正式文件。禁止先 remove 正式文件再 rename。
- 封装层检查可用的写入/关闭错误；Arduino File.flush 无错误返回，不能单凭 flush 声称落盘成功。临时文件重读用于检查内容，rename 失败保持旧正式文件；具体底层 rename 替换语义在实现时核验并用设备测试确认。
- 正式文件替换后再次 read，成功才应用周期；重读失败返回 ReloadFailed，旧运行周期保留，但明确新文件可能已经保存，可 reload 重试。此处不伪装跨文件系统与内存状态的原子事务。
- 临时文件从不作为运行配置读取；遗留临时文件在下一次显式保存时覆盖，不在普通读取或每次启动时反复写盘。意外断电后的正式文件不可读时回退默认并报告错误，不承诺未经测试的断电事务保证。

## 4. 当前修改入口与未来 settings

本次没有设置 UI，为使保存与立即应用可实际使用，在现有本机串口诊断旁增加三条行命令：

```text
codex.config.read
codex.config.reload
codex.config.save {"refreshIntervalSeconds":60}
```

read 返回文件中的值或错误，并同时显示当前实际任务周期；reload 将文件应用；save 保存并立即应用。保留现有小写 `t` 授时诊断，不改变 BLE 协议。协议分片与 UART 输入分别处理。

串口行缓冲最大 576 bytes，每轮最多消费 64 bytes，不使用阻塞 readStringUntil；超长行丢弃到换行并报告错误，不执行截断命令，支持 LF/CRLF。这里只实现固定命令，不建设通用命令框架。文件操作仅在明确命令时同步执行，耗时在真机验证，不进入调度回调；必要时后续再优化。

未来 settings 的展示调用 read，保存调用 save，按结果反馈及置脏；不需要重新实现文件校验和调度更新。

## 5. 桌面与 hello 清理

- desktop 删除 `DEFAULT_REFRESH_INTERVAL_SECONDS`、CodexSettings.refresh_interval_seconds、相关默认回退及 hello.settings.codexRefreshIntervalSeconds；仓库 config.json 删除刷新配置，保留 requestTimeoutSeconds。
- 旧配置若仍有 refreshIntervalSeconds：仅识别为废弃键，忽略其值并给出迁移提示，不继续校验周期、不保留内部字段、不下发。其他未知键继续拒绝；这是兼容入口，不是有效配置项。
- RPC 默认 15 秒统一在 App Server 适配模块定义为 `DEFAULT_RPC_TIMEOUT_SECONDS`，config.py 引用同一常量；客户端构造参数复用它，避免当前两处默认值。仍保留用户配置范围 5～60 秒。
- 固件删除 Message、ConnectionSession 中的刷新字段及 Codec 对旧 settings 的读取；hello 只处理身份、版本、capabilities。CodexController.onSessionReady 不再接收周期参数。
- 协议版本维持 1，新 hello 不携带 settings；旧 hello 的多余 settings 作为未知字段忽略。保留旧 hello fixture，并增加无 settings 的样例，覆盖新桌面/旧字段兼容。
- 原固件在 hello 缺字段时默认 300 秒的行为以当前旧实现建立回归断言；不保留生产旧解析代码只为测试兼容。

## 6. 复用、验证与范围

| 检索/能力 | 决策 |
| --- | --- |
| ScheduledTaskService 生命周期 | 复用 updateInterval/getTask；不新增长期轮询任务 |
| CodexController / Page / main 脏标记 | 扩展配置应用，删除 hello 周期参数及页面重复默认 |
| ArduinoJson 与既有 native fake 模式 | 复用解析库与注入方式；配置单独校验，不使用消息信封 |
| 平台 LittleFS | 框架已提供，新增小文件适配；配置 filesystem 类型，不升级依赖或调整分区 |
| 文件/配置服务 | 项目没有现成能力，新增最小存储接口与 Codex 模块服务；不推广到其他模块 |
| desktop 配置与 hello | 删除刷新链路，保留 RPC 超时和废弃键提示 |

自动化覆盖解析上下界/错误类型/超长/尾部内容、文件每次重新读取、缺失/损坏/挂载失败、相同值不写、不重设起点、写/rename/重读失败、保存后立即应用、暂停与在途状态保留、300↔60 秒、回绕及 I/O 后新时间采样。协议测试覆盖旧 settings 不影响本地周期、新 hello 无 settings；桌面回归保留 RPC 超时。

目标构建与 buildfs 检查平台接口及镜像路径；不自动 upload/uploadfs。真机检查首次安装、串口读写/立即生效、重启持久化、应用刷写保留文件、异常文件恢复、页面与 BLE 响应性。文件部署覆盖整个分区的限制写入根 README。

本次只新增 Codex 周期配置。settings UI、自动开关持久化、其他模块迁移、远程配置和授时参数调整仍在范围外。

需求追踪：R-01→第 1/3 节；R-02→第 2/4 节；R-03→第 2/3/4 节；R-04→第 1/3 节；R-05→第 5 节；R-06→第 4/6 节。

## 待确认问题

无。用户回复“继续”，确认本技术设计，进入任务拆解。尚未修改生产代码或设备分区。
