<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 变更记录

本文件记录面向使用者的重要变化。版本尚未发布的内容统一保留在“未发布”章节。

## 未发布

## 0.26.0 - 2026-09-05

- 增加 Frame Packet 构造与复制量、渲染阶段耗时、队列深度、高水位、替换、拒绝及节流计数，
  并通过现有诊断通道输出运行摘要。
- Frame Packet 在完成回执后进入三槽回收池，复用逐帧 CPU 容器且不破坏提交后的不可变所有权。
- Mesh、Material 与 Texture 改为不可变共享资源快照，普通帧不再重复复制顶点、索引和像素正文。
- 区分可替换实时帧与必须完成帧；后者具有序列和独立完成回执，不会被新实时帧静默替换。
- 增加构造前背压：待处理 Frame 达到上限时跳过昂贵 Scene Snapshot 和 Packet 捕获，同时继续处理
  窗口事件、输入和游戏逻辑。

## 0.25.0 - 2026-09-05

- 将 Granit 依赖升级至 0.7.0，Fetch 锁定发布提交，PACKAGE 与安装消费最低版本同步提升至 0.7。
- 新增自有 Render Frame Packet，提交后不借用 Scene、资源、UI、Debug Draw 或下一帧可变内存。
- 新增有界 Frame/Command 渲染执行器、单调序列号和完成回执；积压 Frame 可替换，Command 保持
  FIFO 且过载明确返回 `not_ready`。
- Renderer、Swapchain、Pipeline、GPU 资源镜像、上传、录制、提交、Present 和销毁统一由常驻
  渲染线程串行执行，主线程继续负责窗口事件、逻辑和 UI 构建。
- Command 支持线程安全的准备/上传阶段与工作量报告，并可查询运行中及最终状态；失败命令不会
  终止执行器或提交半成品资源句柄。
- Resize、最小化、Out-of-date、初始化失败与关闭纳入确定的排空、重试、逆序释放和 Join 边界。

## 0.24.0 - 2026-09-05

- 新增基于稳定作者 UUID 的 Scene 结构差异与事务式热重载，匹配节点保留实体身份，失败时完整保留
  旧场景及资源状态。
- Prefab 来源变化会批量刷新当前场景的全部同源实例，同时保持匹配身份和实例局部字段覆盖；任一
  候选无效时整批回滚。
- Asset IPC 支持 Scene 与 Prefab 结构修订，并复用串行修订、请求关联、能力协商和断线重同步；
  Runtime 只在主线程安全点提交结构变化。
- Editor 监视作者结构资产并过滤自身保存与重复事件；干净文档自动接受相关外部变化，未保存文档
  进入冲突状态且不会被静默覆盖。
- Lantern Gallery 自动化覆盖 Scene、Prefab、身份保持、覆盖传播、损坏资源回滚、连续修订与 Runtime
  重启；Windows 与 Linux Shared/Static、Clang/GCC、Granit Runtime 和 Sanitizer 验收通过。

## 0.23.0 - 2026-09-05

- 将 Granit 固定版本更新至 `eb970c74570e278678ee39530c68afc40101879f`（v0.5.0 发布基线）。
- C++ `gneiss::result` 改为保持单个 32 位结果码的轻量值类型，新增 `ok()`、`failed()`、显式布尔
  转换、`native()` 和 `message()`，同时保留既有结果常量、C ABI 及其转换函数。
- Granit `PACKAGE` 模式最低要求 0.5；Fetch 默认提交采用可追踪缓存升级语义，保留用户显式版本
  覆盖，并新增独立的 CMake 配置回归测试。

## 0.22.0 - 2026-09-04

- Editor 递归监视工程源资产，以规范化路径、稳定读取、内容校验和防抖队列过滤重复或未完成的
  文件系统事件，并自动复用现有导入事务。
- 新增独立 Asset IPC 协议域，以类型、修订号和请求关联传递增量重载与全量重同步命令，不通过
  控制通道传输资产正文。
- Runtime 在主线程安全点按 Texture、Material、静态 Mesh 的依赖顺序事务式加载候选资源；失败
  保留旧缓存映射和已有租约，成功更新不改变场景节点与实体身份。
- Editor 在手动或自动导入成功后发布资产修订，并在 Asset Browser 展示等待、应用中、已应用、
  失败及需要重启状态；Runtime 重连后自动重同步已知资产快照。
- Windows、Linux Clang/GCC Shared/Static、Granit Runtime 无头测试与 Sanitizer 验收矩阵均通过。

## 0.21.0 - 2026-09-04

- Editor–Runtime IPC 升级为协议 v2，以协议域、域内操作、语义标志和请求 ID 组成统一信封，删除
  尚未发布的 v1 帧与全局消息类型。
- 建立协议注册表与统一 Dispatcher，集中校验握手状态、消息方向、协商能力、负载预算和未知操作。
- Session、Control、Log、Inspection、Statistics 与 Property 分别拥有独立编解码和双端处理器，
  Editor 与 Runtime 通过强类型命令和事件队列组合会话。
- 保持既有 I/O 线程、主线程安全点、有界队列、控制优先级和本机回环 TCP 行为，并完成 Windows、
  Linux 全矩阵及 Sanitizer 验证。

## 0.20.0 - 2026-09-03

- Editor 可将普通场景子树创建为 Prefab，以原子作者事务同时写入来源资产并用引用实例替换原子树；
  含嵌套 Prefab 的子树会被明确拒绝。
- Editor 可将实例已有的 Transform 字段覆盖应用回共享 Prefab 来源，提交前检查来源修订，成功后
  清除已应用覆盖并刷新全部同源实例。
- Editor 可将 Prefab 实例 Unpack 为拥有新稳定 UUID 的普通作者节点，同时保留当前可见层级、组件、
  Transform 和实例覆盖结果。
- Create、Apply 与 Unpack 已接入层级和 Inspector 的确认操作、Undo/Redo、选择恢复、脏状态、冲突
  反馈与失败回滚。
- Lantern Gallery 自动化在临时工程副本中覆盖 Create、Apply、Unpack、Undo/Redo 和保存重开；
  Windows、Linux Core、Granit Runtime 与 Sanitizer 验收矩阵均通过。

## 0.19.0 - 2026-09-03

- 场景格式升级到 v4，以实例 UUID、来源节点 UUID、Type ID 和 Field ID 保存类型安全、确定排序的
  稀疏 Prefab 字段覆盖；项目尚未发布，因此旧场景版本直接拒绝而不积累迁移代码。
- Prefab 实例化与刷新通过冻结的类型注册表验证并应用覆盖；刷新失败保留旧投影，同源多个实例的
  作者值、资源租约和 Runtime 状态互相隔离。
- Editor Inspector 与 Transform Gizmo 可编辑 Prefab 来源节点的实例局部 Transform，并显示来源值、
  覆盖状态以及字段级和整 Transform 恢复操作，完整接入 Undo/Redo、脏状态与保存流程。
- Runtime 检查协议升级到 1.3 和 `runtime_inspection_v2` 能力，使用实例 UUID 与来源 UUID 传递复合
  作者身份；显式回写会更新当前实例覆盖，不修改共享 Prefab 资产。
- Lantern Gallery 的三个同源灯笼实例分别覆盖灯体平移、框架缩放和玻璃平移，并由端到端 Runtime
  工作流验证差异化投影及复合身份。

## 0.18.0 - 2026-09-03

- 增加独立 `gneiss.prefab` v1 作者格式、严格结构校验、VFS Loader 与统一资源缓存；Prefab 节点
  复用现有场景组件 Schema，并使用稳定源节点 UUID。
- 场景格式升级到 v3，以 Prefab URI、实例 UUID、父级、名称和实例根 Transform 保存紧凑引用；
  加载时原子创建独立 Runtime 投影，保存时不写入展开副本。
- 建立实例 UUID 与源节点 UUID 组成的复合作者身份，支持同源多实例、独立资源租约、失败回滚、
  销毁后旧句柄失效和 v2 场景兼容迁移。
- Editor 支持从 Asset Browser 放置 Prefab，在 Hierarchy 中展示实例边界与只读来源节点，并允许
  对实例根执行重命名、Transform、复制、删除、Undo/Redo 和显式刷新。
- 同源 Prefab 实例可作为一条原子命令统一刷新；失败保留旧投影，成功刷新可在新旧来源版本间
  撤销和重做，同时恢复有效选择。
- Lantern Gallery 使用一个项目自有 Prefab 复用三组灯笼，补齐可读节点名称，并覆盖源更新传播、
  独立根 Transform、保存重开和 Runtime Play 工作流。
- 修复 Linux CI 获取 Dear ImGui docking 锁定提交时的浅克隆问题；Windows、Linux Core、Granit
  Runtime 和 Sanitizer 验收矩阵均通过。

## 0.17.0 - 2026-09-02

- 增加版本化 Runtime 属性写入协议与能力协商，通过 Type ID、Field ID、对象 generation 和期望
  修订号提供类型安全的寻址、冲突检测与稳定错误反馈。
- Runtime 在主线程安全点执行有界属性命令；属性流量不会阻塞停止控制，断线或新会话也不会自动
  重放旧写入。
- Editor Runtime Inspector 支持编辑 Transform 平移、欧拉角展示对应的四元数和缩放，并展示
  等待、成功、拒绝、超时、断线及被运行逻辑覆盖状态。
- 增加将 Runtime Transform 显式应用到作者场景的操作，复用既有撤销、重做、脏状态和保存流程。
- Lantern Gallery 覆盖运行中写入、暂停稳定、游戏逻辑覆盖和连续会话隔离；Windows、Linux、
  Granit Runtime 与 Sanitizer 验收矩阵均通过。

## 0.16.0 - 2026-09-02

- 建立 Runtime 权威场景采样与 Editor 只读镜像，支持按稳定对象标识同步层级、Transform 与组件
  属性，不共享 World、Scene Tree 或资源句柄。
- Editor 在运行期间切换到 Runtime Hierarchy 与 Inspector，停止后恢复作者场景；镜像更新保持
  有界并处理断线、慢消费者、连续 Play 和对象生命周期变化。
- 增加基础运行统计、协议流控和恢复语义，并通过 Lantern Gallery 验证真实游戏模块更新、暂停、
  恢复与新会话隔离。
- Windows、Linux、Granit Runtime 与 Sanitizer 验收矩阵均通过。

## 0.15.0 - 2026-09-02

- 基于 libuv 增加跨平台 I/O Runtime、版本化 IPC 帧与本机传输，Editor 作为服务端、Runtime 作为
  客户端建立带鉴权和能力协商的独立控制通道。
- 增加 Runtime 启动、暂停、恢复、停止、心跳、状态与故障协议，并保持 UI、I/O 和 Runtime 主线程
  的所有权边界。
- Editor 运行控制栏和 Console 接入双向会话状态，覆盖握手超时、异常退出、强制停止、重连及连续
  Play；旧的停止信号文件协议被移除。
- Windows、Linux、Granit Runtime 与 Sanitizer 验收矩阵均通过。

## 0.14.0 - 2026-09-02

- Dear ImGui 更新到锁定的 Docking 分支提交，并建立覆盖主客户区的单窗口 DockSpace；Hierarchy、
  Assets、Scene View、Inspector 和 Console 支持拖动、拆分、吸附、关闭及菜单恢复。
- 抽取 Editor 私有 `gneiss_editor_ui` 模块，统一管理 ImGui Context、字体、主题、帧适配、主工作区
  和运行控制图标，不向 Engine、Runtime 或公共接口传播 ImGui 类型。
- 增加确定性五区默认布局、按工程隔离的版本化布局文件、面板可见性持久化、v1 到 v2 迁移、原子
  保存、损坏回退及 `Reset Layout`。
- 修复 DockSpace 背景遮挡 3D 场景，并在窗口失焦时使用 Gneiss 输入快照复位 ImGui 键盘和鼠标状态，
  避免 Runtime 切换后出现卡键。
- 调整 Editor 字体采样、图标几何、最小面板尺寸和主题对比度，并增加 Noto Sans SC 中文回退；完整
  字体、多语言及未来自研 GUI 体系不属于本版本。
- Windows VS2022 Debug/Release Shared/Static、Windows Clang Debug、Linux GCC/Clang
  Shared/Static、Sanitizer 和 Granit 运行时无头矩阵均通过。

## 0.13.0 - 2026-09-01

- 增加 Experimental 结构化日志消息、不可变事件及 C11/C++20 提交接口，明确复制所有权、可信来源、
  事件顺序、线程安全和诊断转换语义。
- 增加每个 Application 独立的有界异步日志队列、串行 Sink 投递、背压丢弃报告和关闭排空，并将
  Runtime 标准流与轮转文件接入统一事件链路。
- 向 Experimental Game Context 增加模块日志入口，由 Runtime 绑定可信模块来源；Lantern Gallery
  游戏模块会记录初始化与关闭事件。
- 定义带 `@gneiss-log-v1` 前缀的版本化 JSON Lines 跨进程协议，支持特殊字符、分块读取、超长行
  恢复、未知版本和原始输出降级。
- Editor 增加有界 Console 数据模型与 Runtime 会话隔离，支持级别、来源、分类、Raw、当前会话和
  文本组合筛选，以及暂停显示、清空、复制、丢弃计数和自动滚动。
- `child_process` 增加不影响历史输出的增量读取通道，Editor 可持续解析 Runtime 结构化事件并在
  进程退出时冲刷尾部输出。
- Editor 主窗口支持缩放，浮动面板可移动和调整大小，顶部增加运行、暂停占位和停止控制栏；完整
  Docking、布局持久化与工作区体验延期到后续 Editor UI 改造。
- Windows 本地 92/92 测试、隔离安装树、Linux Clang/GCC 共享/静态、Granit 无头运行、Sanitizer、
  Windows MSVC Runtime 和安装后 Consumer 矩阵均通过。

## 0.12.0 - 2026-08-31

- 增加 Experimental Game Module C ABI、强类型 C++ Game Context 包装和模块描述校验，定义原生模块
  查询入口及初始化、固定更新、逐帧更新和关闭回调契约。
- 增加 Win32/POSIX 原生动态库后端及 Runtime 模块会话，保证模块状态关闭后再卸载动态库，并覆盖
  缺失文件、缺失符号、ABI 不匹配和生命周期失败路径。
- 增加受线程和生命周期约束的 Game Context，可借用 World 与启动场景根实体、读取输入动作并请求
  Runtime 正常退出。
- 增加有界固定步长与逐帧更新调度器，支持长帧裁剪、最大追赶次数、积压丢弃报告及回调失败传播。
- 工程格式 v2 增加可选原生游戏模块定位与受约束 CMake 构建字段；Runtime 可从工程根内加载模块并
  接入 Game Context、固定更新、逐帧更新和逆序关闭，同时保持 v1 无模块工程兼容。
- Editor 对含模块工程执行异步 CMake preset/target 构建，成功并验证产物后才启动 Runtime；失败或
  中止时保留作者会话与构建输出，且不会运行旧模块。
- 工程格式 v2 支持可选启动输入映射；Lantern Gallery 增加独立动态游戏模块，通过 A/D 动作
  旋转灯笼场景根节点，并覆盖 Runtime 与 Editor 构建树工作流。

## 0.11.0 - 2026-08-31

- 将工程描述解析从 Editor 提取为无 UI 的内部应用宿主模块，并增加失败阶段、结果码与路径报告，
  为独立 Runtime 宿主复用工程运行契约。
- 增加实验性 `gneiss_runtime` 工程运行入口、三帧 smoke 模式、结构化控制台日志、可覆盖路径的
  1 MiB 单备份轮转文件日志，以及包含 Engine 与 Granit 动态库的安装规则。
- Editor 增加 Runtime 运行准备策略；脏场景必须显式保存，启动请求只携带工程根，不共享作者
  World、Scene Instance、撤销栈或资源句柄。
- Runtime 增加实验性的停止信号文件协议，收到 Editor 请求后通过 Application 正常退出。
- Windows Editor 增加 Run/Stop、保存并运行确认、重复启动保护及捕获 Runtime 标准输出/错误的
  Runtime Output 窗口。
- 抽取内部 `child_process` 跨平台子进程层，由 Windows 与 POSIX 后端统一提供参数传递、输出捕获、
  退出状态和强制终止；Editor 的 Runtime 层仅保留工程启动与正常停止协议。
- 补齐 Runtime 启动失败、非零退出、重复运行、停止超时和再次运行验收；Editor 显示并保留每次
  Runtime 会话日志路径。
- Editor Demo 作为完整工程安装，并增加构建树 Lantern Gallery 与隔离安装树 Editor Demo 的
  `gneiss_runtime` Smoke 验收。
- 将完整运行库及 CMake target 更名为 `gneiss_engine`、`gneiss::engine`，工程运行宿主统一命名为
  `gneiss_runtime`。

## 0.10.0 - 2026-08-29

- 公共 C ABI 增加 Stable/Experimental 逐符号分类及实验性声明标记。
- 公开平台、纹理和属性类别改为定宽常量，反射查询输出增加版本化 `struct_size`。
- 增加稳定运行时代表性样例、冻结头兼容测试和隔离源码树的 Shared/Static 安装 Consumer。
- 增加可重复的 Release 性能/内存采样、故障注入矩阵、Sanitizer 检查和 GPU 逻辑资源退出检查。
- Granit 更新至资源统计合并提交，Application 销毁时会报告未释放 GPU 逻辑资源的分类计数。
- 安装树补充项目许可证、变更记录、第三方声明及随 Runtime 分发的第三方许可证原文。
- 0.9.0 调用方升级方式见[迁移指南](docs/guides/migrating-0.9-to-0.10.md)。

## 0.9.0 - 2026-08-28

- 扩展 Scene Instance 作者修改接口，支持空节点创建、重命名、重挂接、子树复制、删除与恢复。
- 完善 Editor 命令历史、层级创作和 Camera、Mesh Renderer 组件操作，连续属性编辑可合并撤销。
- 增加 New、Open、Save、Save As 与未保存修改确认，场景文档可在工程资产根内完整流转。
- 接入 ImGuizmo Transform 操纵器、深度测试世界网格和坐标轴，Inspector 使用 XYZ 欧拉角编辑旋转。
- 完善无参数 Project Manager 启动、分阶段错误诊断及 Lantern Gallery 场景创作验收工作流。

## 0.8.0 - 2026-08-28

- 增加 Editor 共用资产导入 SDK、版本化资产索引和 Asset Browser，支持 glTF/GLB 导入、状态检测与
  重新导入。
- 增加 Scene Instance 作者编辑接口，支持创建 Mesh Renderer 节点、替换资源、删除叶节点并保留
  未知场景字段。
- 增加 Editor 命令历史，支持属性修改、节点创建、资源替换和节点删除的撤销与重做。
- 增加 Lantern Gallery 端到端工作流测试，覆盖导入、过期检测、重新导入、场景放置、Undo/Redo、
  保存和重载。

## 0.7.0 - 2026-08-28

- 增加独立 Gneiss Editor，提供场景层级、选择、独立相机、反射属性检查、脏状态和原子保存闭环。
- 增加版本化工程文件、Project Manager、最近工程及原子创建最小工程，并以工程根作为统一入口。
- 接入固定版本 Dear ImGui、后端无关 UI Draw List、`Gneiss Mocha` 主题和 Inter 界面字体。
- 将 Editor Demo 与 Lantern Gallery 组装为可直接打开的工程，并增加 Editor 端到端冒烟测试。
- Windows Clang/MSVC 与 Linux Clang/GCC 共享/静态矩阵、安装 Consumer 及无头 Editor 图形验证通过。

## 0.6.0 - 2026-08-28

- 增加稳定 Type ID、Field ID、Type Registry 元数据注册/冻结/查询接口及 C++20 RAII 包装。
- 增加类型安全属性值、字段 getter/setter 绑定、统一校验和 C11/C++20 属性读写接口。
- 将 Scene Tree Transform 与 ECS Camera 接入内建反射注册和 World/Entity 属性访问路径。
- 将场景 Schema 升级到 v2，增加 v1 逐步迁移、未知字段保留及当前格式重新序列化。
- 增加场景实例属性同步序列化接口和无 UI 属性检查示例，覆盖修改、保存与重新加载。
- 增加基于 fastgltf 的离线 glTF/GLB 资产编译器，支持确定性、事务式输出和多 Primitive 拆分。
- 增加版本化 Mesh Binary v1、资产检查命令及运行时二进制加载路径，运行时不依赖 fastgltf。
- 增加 CC0 Lantern Gallery 导入场景，并将索引数据从导入、存储一直保留到 Granit Indexed Draw。
- Granit 接入升级至 `0.4.0`，使用动态 Uniform Offset 和设备限制查询管理逐对象数据。
- 将静态 Mesh 打包到持久 GPU 几何 Arena，同一 Mesh 的多个实例复用几何数据。

## 0.5.0 - 2026-08-27

- 增加版本化 Camera、活动 Camera 管理、右手视图与 Vulkan 透视投影约定。
- Granit 路径增加 D32 深度缓冲、真实裁剪空间投影和与绘制顺序无关的 3D 遮挡。
- 增加 Mesh v3 逐顶点法线、逆转置法线变换、方向光和环境光，保持旧 Mesh 无光照兼容。
- 将原创片麻岩神殿升级为立体地面、石柱、横梁与祭坛场景，并支持 `A`/`D` 轨道观察。
- 将神殿配套资产收拢到示例目录，并支持从构建树或安装前缀独立定位。
- 将三角形 fixture 迁入测试数据目录，正式引擎安装包不再携带通用资产目录。
- 确定 glTF 离线导入边界与后续资产编译器范围，工具层采用 fastgltf，运行时保持解耦。

## 0.4.0 - 2026-08-27

- 增加 Texture C11/C++20 契约、RGBA8 像素格式、线性/sRGB 颜色空间和 RID 生命周期。
- 使用内部 libspng/miniz 从 VFS 解码 PNG，并通过版本化 Texture 描述创建缓存租约。
- 增加 Mesh/Material v2 的 UV、base-color Texture URI 和依赖租约，保持 v1 资产兼容。
- Granit 后端增加 Texture/View 镜像、Sampler、材质 Bind Group 批次和默认白纹理。
- 增加三张原创纹理及可交互的 2.5D 片麻岩神殿示例，支持显式 Z 绘制层级。
- 本地 Windows Clang 静态核心、共享 Granit、安装 Consumer 和真实纹理场景验收通过。

## 0.3.0 - 2026-08-27

- 增加后端无关的 C11 输入 ABI、C++20 包装、帧状态快照和固定容量原始事件队列。
- 接入 Granit Input 后端、焦点清理与无输入座席降级，保持无头环境的窗口和渲染能力。
- 增加版本化动作映射 JSON v1、VFS 事务加载、代次句柄和 Application 隔离。
- 增加 Application 级同步诊断回调，以及稳定的严重度、类别、结果码、模块和消息字段。
- 将资产驱动三角形示例改为通过动作映射响应 `A`、`D` 和 `Esc`。
- 完成 Windows/Linux、共享/静态、Granit 运行时及安装后 C11/C++20 Consumer 验收。

## 0.2.0 - 2026-08-26

- 锁定 yyjson `0.12.0` 作为内部 JSON 解析依赖，并验证严格 UTF-8、精确整数和错误位置行为。
- 增加严格的 `asset://` URI、可挂载 VFS、本地文件系统目录逃逸防护和内部资源缓存基础。
- 增加版本化场景 Schema v1、VFS 读取、纯中间描述和 UUID、层级、组件字段完整校验。
- 增加 Mesh/Material JSON v1、VFS Loader、RID 缓存租约和失败重试闭环。
- 增加原子场景实例加载、卸载、UUID 节点查询，并将三角形示例迁移为完全由资产驱动。
- 增加可重定位的 CMake package、安装资产目录及共享/静态 C11、C++20 Consumer 验收。

## 0.1.0 - 2026-08-26

- 建立 C11 公共 ABI 和轻量 C++20 包装。
- 增加 Application 生命周期、时间、暂停、退出与 Granit Window 平台适配。
- 增加 World、Entity、确定性 System 调度和基于 EnTT 的内部 ECS 存储。
- 增加 Scene Tree、实体映射与层级 Transform。
- 增加 Mesh、Material RID、Camera、Mesh Renderer 和 World 渲染快照。
- 增加基于 Granit 的 Triangle List 渲染闭环、固定帧数 smoke test 和旋转三角形示例。
- 增加 Granit 父工程、已安装 package 与锁定源码下载三种依赖解析路径。
