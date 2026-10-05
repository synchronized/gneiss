<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 变更记录

本文件记录面向使用者的重要变化。版本尚未发布的内容统一保留在“未发布”章节。

## 未发布

- 内部实现按 Engine 的 Core/Platform/Asset/Function 分层，C ABI 适配集中到 `src/engine/api/`；公共头路径保持兼容。
- Editor 模型、会话、资产服务与面板归 `src/editor/`，离线库归 `src/tooling/`；根目录 `apps/` 保留入口及宿主装配。
- 补齐 102 个公共 C 函数的 C++ 映射及类型、常量、回调审计，增加拥有/借用视图和 Prefab 刷新令牌 RAII。
- 修复 Prefab 子树捕获包含无关节点、层级树拖拽期间刷新节点数组，以及创作命令失败时的回滚边界。
- 增加分层违规反例、独立头与安装消费者验证，以及不创建原生窗口的面板交互测试。
- 本版不升级 Granit；0.44 的大场景 cooperative 长帧和预算观测限制继续保留。

## 0.44.0 - 2026-10-01

- 接入运行纹理设备变体选择、独立负载读取、上传后 CPU 负载释放及原内容重建来源。
- 增加场景候选、Application 共享资源和上传暂存预算统计，预算拒绝返回详细字节诊断。
- Editor/Runtime 共享场景加载预算反馈；场景取消、失败和重试保持旧场景不变。
- 完整 Cook Sponza 的 Windows thread 模式已验证候选数据低于 2 GiB，并完成切换、取消、失败和窗口自动化回归。
- 完成本地 Windows Clang 164/164，以及远端 Linux 8/8、Windows 4/4 构建与测试矩阵。
- cooperative 模式的大场景仍可能出现主线程长帧；取消后的预算归零没有独立外部观测字段，真实独立 GPU 显存未测量。

## 0.43.0 - 2026-09-30

- 升级 Granit 至 0.43.0，接通五类 PBR 贴图、法线切线空间、完整 Mip 和负缩放修正。
- Material v5 保存逐槽 UV/采样、OPAQUE/MASK/BLEND 与双面；Mesh Binary v3 保存 UV1/顶点色。
  旧文件及 C 描述保持兼容，glTF 导入器版本 3 使旧缓存失效。
- 使用 MikkTSpace 生成缺失或退化切线，保留修复诊断；按颜色、数据、法线用途生成正确的 Mip。
- 多贴图依赖作为同一异步事务发布；采样器由材质 GPU 镜像持有，复用已有帧资源生命周期。
- 修复候选累计驻留漏算切线、UV1/顶点色，以及内存预算不足误报 PNG 尺寸错误的问题。
- 完整 4K Sponza 超出 2 GiB CPU 候选预算时明确拒绝；不丢贴图，不自动提高预算。
  对象级透明排序不保证相交三角形正确，不包含折射/OIT、完整 glTF 灯光或完整引擎 Web 移植。

## 0.42.0 - 2026-09-28

- Runtime 启动与同工程完整场景切换改用后台描述准备、有界资产子批次和分阶段实例化；候选使用独立
  World 与缓存，激活前失败或取消保留旧场景。公共同步接口与 ABI 保持不变。
- Editor 增加完整场景加载进度、取消与重试；独立 `scene` IPC 域协商能力，切换后刷新检查镜像和
  Game Context，游戏模块按每次激活重新初始化。
- 支持有限但超出 `[0,1]` 的平铺 UV，以及未声明 MIME 的外部 PNG；通过固定 Intel Sponza 资产
  建立同步与异步测量工具，第三方大场景资产不随源码分发。
- 修正 Windows 停止测试脚本编码，以及退出后心跳错误掩盖已收到关闭终态的问题。
- 日志洪泛时为 IPC 控制消息保留发送容量，接收队列优先淘汰日志，避免暂停确认等消息被日志挤掉。
- `--smoke` 等待场景加载终态；候选依赖上传失败统一返回场景加载退出码 4。协作模式的单次解析、
  哈希及解码仍不可抢占；结构热重载与完整引擎 Web 不属于本次异步场景范围。

## 0.41.0 - 2026-09-25

- 混合 Mesh/Material/Texture 修订在共享执行器完成依赖准备，GPU 全批确认后发布；保持已有 RID，
  失败保留旧画面，支持线程池与协作模式。
- 网格改用独立 GPU 缓冲，按资源数和字节预算分批上传；单资源和协作任务仍不可抢占。
- Editor 显示准备/上传进度，提交许可前可取消并重试；Asset IPC 域升级为 3，公共 ABI 不变。
- 新增真实 GPU 像素回归和 Lantern 模型窗口测量；初始场景与结构修订仍同步，完整 Web 不在本版范围。

## 0.40.0 - 2026-09-25

- 内部任务调度器支持显式线程池与协作模式，保持作用域、依赖、取消和有界完成回执语义；
  Editor 与 Runtime 注入宿主执行器，Application 不再需要另建任务池。
- 新增纹理 CPU 异步准备及 GPU 确认后发布，支持 PNG、KTX2 和运行纹理封装；失败保留旧资源，
  Runtime 仅在实际完成后回复修订结果，队列与候选字节均受限。
- 纯纹理更新保留 RID 与材质引用，卸载、同步替换后的迟到候选不能复活旧资源。
- 修复连续 resize 时排队帧尺寸与交换链不一致导致的渲染参数错误。
- 新增最小 Emscripten 无线程浏览器验证及大纹理窗口测量；完整引擎 Web 尚未支持，协作模式中的
  单个解码不可抢占，初始场景及混合资产批次仍同步。内部使用方式见[调度指南](docs/guides/task-scheduling.md)。

## 0.39.0 - 2026-09-25

- 新增内部有界任务池、依赖、串行队列、取消作用域、完成预算和耗时诊断；不扩展公共 ABI。
- Editor 的导入和作者文件检查共享线程资源，保留工程串行写入、暂停导出及取消后提交规则。
- Scene/Prefab 枚举和哈希移至后台；保存使用实际写入内容建立基线，旧检查不能覆盖新保存。
- 冲突判断、文档加载和 Scene/ECS 应用仍在主线程；渲染及 libuv 继续使用专用执行器。
- 加入确定性并发回归和实际模型/多 Prefab 基准，详见
  [0.39 验收记录](docs/records/M-250-255-0.39.0-validation.md)。

## 0.38.0 - 2026-09-25

- Editor 的源文件检查、哈希、手动/自动导入和 Asset Browser 刷新移至单工作线程，
  界面保留快照并显示阶段、排队数量、取消及失败重试入口。
- 导入先暂存并复核源哈希，取消或过期任务不提交；索引保存失败回滚旧派生目录。
- 关闭时取消未提交任务并等待工作线程，导出期间暂停资产写入；增加生命周期、并发和原生窗口回归。
- 作者 Scene/Prefab 监视与应用仍在主线程，外部依赖哈希及跨进程事务隔离尚不支持。
  Windows Debug 调试堆仍可能在大块解析分配期间造成停顿；Release 与 Debug 测量分别记录于
  [0.38 验收记录](docs/records/M-246-249-0.38.0-validation.md)。

## 0.37.0 - 2026-09-25

- Gizmo 拖动按原节点身份统一收尾；切换/清空选择、隐藏视图和释放不再丢失撤销记录，
  历史记录失败回滚原节点；单次多帧拖动生成一条 Undo/Redo 命令。
- 修复非根子树快照包含外部父引用，导致删除子节点后无法撤销的问题；
  Delete 快捷操作前先收尾拖动，连续撤销能依次恢复删除和拖动前的变换。
- Gizmo 使用当前操作的悬停命中，投影裁剪面与 Editor Camera 对齐，拒绝非法透视模型矩阵。
- 增加根/父子节点及 Prefab 变换事务回归、四组尺寸的真实 ImGuizmo 输入回放，
  以及原生 Win32 连续 resize、最小化恢复和合成 DPI/输入一致性回归。
- 新增可复现的资产响应性基准；确认大文件哈希和重导入仍会同步阻塞，后台化待独立实施。
  本版未加入完整几何拾取、SDL3 切换或物理多显示器人工验收；详见
  [0.37 验收记录](docs/records/M-242-245-0.37.0-validation.md)。

## 0.36.0 - 2026-09-25

- Scene/Prefab 作者文件丢事件后增量补扫新增、修改和删除，保留未保存场景冲突保护；
  Runtime 资产修订同步发布失败现在单独显示错误，避免导入成功被误认为运行时已更新。
- 增加导入、源材质修改、补扫重导入、Runtime 应用、目录包导出与包内启动的连续回归。

- Granit 依赖基线升级至 0.30.0，同步 FETCH 缓存迁移、PACKAGE 和安装 Consumer 最低版本；
  继续使用原生 Window 后端，未启用 SDL3。

- 源文件监听或重导入队列丢失事件后，自动分帧检查索引中的已导入源文件，找回修改与删除；
  提供手动检查入口，候选入队及哈希检查受每帧数量预算限制。

- Asset Browser 显示源文件与作者资产监听错误并提供重启入口；事件丢失显示累计告警。
  修复监听启动失败时句柄提前释放的问题，重启监听不再带入上一轮未消费事件。

- Asset Browser 在 Runtime 热更新失败后提供显式重新同步入口；修复资产文件后可恢复，
  同步期间重复点击不重复排队，需要重启的变更则提示停止并重新启动 Runtime。

- 编辑器重导入回调异常会报告源路径与失败原因，结束当前任务并继续处理后续候选；再次收到
  修改通知后可重试，避免同一异常任务每帧重复执行。

## 0.35.0 - 2026-09-23

- PNG Cook 产物改为包含 BC7 优选变体和 RGBA8 回退的 `.gneiss-texture`，保留完整 Mip 链与
  颜色空间；Runtime 按设备能力选择变体，通过 Upload Batch 提交，失败不替换旧资源。
- `gneiss_assetc inspect/validate` 支持运行纹理封装，检查全部变体的 SHA-256 并输出 Mip 布局；
  渲染日志记录实际选择的纹理变体、批次上传阶段与结果。
- 主循环结束前回收渲染线程结果，修复短程 smoke 可能遗漏末尾纹理上传失败而返回成功的问题。
- 缩短 Granit FETCH 构建目录，修复 Windows MSVC Static preset 的 AssetTools 中间文件路径
  超过 MAX_PATH 导致的 MSB3491；安装验收使用实际依赖构建目录。

- Granit 基线升级至 0.29.1，移除相机的旧 Vulkan Y 翻转补偿，使场景与调试图形方向一致；
  保留无可见物体时清屏和提交 UI 的修复；接入统一 Window/Input、
  强类型 C++ 资源引用和 AssetTools 头文件，移除独立 Input 运行时依赖。
- 接入无阴影投射物时继续渲染的上游修复，解决编辑器拉远后持续无法呈现的问题；Shader Library
  更新为 schema 2，升级时需要重新配置、构建和安装，不能混用旧动态库与 Shader Library。
- 编辑器网格增加密度与距离渐隐，平滑缩放层级过渡，并与当前帧相机同步。
- 编辑器在可选的 `sources` 目录缺失时等待，目录创建后自动启动监听，不再误报启动失败。
- 标准 PBR 接入 Shader Library；离线纹理 Manifest 改由独立 AssetTools SDK 构建，并更新纹理
  处理器缓存版本。Runtime 不依赖 AssetTools。

## 0.34.0 - 2026-09-07

- Granit 基线升级至 0.17.0，并继续通过稳定的公开 Texture 契约隔离 GPU 后端。
- 资产构建将 PNG 确定性转换为无超级压缩 RGBA8 KTX2，生成完整 Mip 链、重写运行 URI，并让颜色
  空间变化参与缓存失效；发布包不再保留已转换的 PNG。
- Runtime 新增受约束 KTX2 校验和多 Mip 资源模型，Granit 镜像会创建完整 Mip 数并逐级上传；损坏、
  越界、格式不匹配及不支持压缩会明确失败。
- `gneiss_assetc cook` 可独立构建运行资产，`inspect` 可查看 KTX2 尺寸、Mip、VkFormat 与 Alpha；
  Editor 发布流程继续报告逐资产进度、构建数和缓存命中数。

## 0.33.0 - 2026-09-06

- 发布管线新增确定性资产构建图和按平台、架构、配置隔离的内容寻址缓存；缓存键包含源内容、处理器
  版本及传递依赖，写入与最终目录均采用事务式提交。
- 首批 Texture、静态 Mesh、Material 与 JSON 处理器会在进入缓存前校验现有 Runtime 资产；KTX2
  探针覆盖 Mip、色彩空间、Alpha 和超级压缩元数据。
- 工程格式 v5 新增 `asset_build.retain`；Shipping 从启动场景、输入、环境和显式保留项计算可达集，
  裁剪未引用资产及作者源文件。
- `gneiss_project` 和 Editor 发布流程共享同一构建服务，展示逐资产进度、缓存命中与裁剪统计；
  Development 包保留无绝对路径的构建诊断元数据。

## 0.32.0 - 2026-09-06

- 工程格式 v4 为 Debug、Development 与 Shipping 显式映射独立 CMake 预设和模块输出目录；旧工程
  继续按单一 Debug 配置加载。
- `gneiss_project package` 统一执行配置、构建、目录收集、清单校验和可选 ZIP 归档，Runtime 可用
  `--profile` 加载对应配置的 Game Module。
- 发布包新增稳定排序的 SHA-256 清单和 `gneiss_project verify` 完整性校验；相同输入生成确定性 ZIP。
- Editor File 菜单新增发布包对话框，可选择构建配置、输出目录和 ZIP，并在独立进程中展示进度及
  失败输出。

## 0.31.0 - 2026-09-06

- Project Manager 改为从正式游戏模板事务式创建工程，并为每个工程生成独立模块 ID。
- Editor 运行游戏工程时自动依次执行 CMake 配置和模块构建，任一阶段失败均不会启动 Runtime。
- 新增 `gneiss_project create` 与 `gneiss_project export`；导出物是仅含运行资产、模块、Runtime、
  动态依赖和平台启动脚本的可检查目录包。
- 安装验收覆盖 SDK 外创建、配置、构建、导出和从目录包启动的完整闭环。

## 0.30.0 - 2026-09-06

- 重新审计 0.10.0 之后新增的公共运行时接口，补齐 10 个遗漏的 Experimental 导出分类；API 门禁
  现在直接核对公共 C 声明、ABI 基线、稳定性清单及声明标记。
- 新增可复制的最小游戏工程模板，使用安装后的 `gneiss::engine` 构建原生 Game Module，并展示
  初始化、逐帧更新、结构化日志和确定关闭。
- SDK 安装模板并通过 `GNEISS_GAME_TEMPLATE_DIR` 暴露位置；安装验收会在源码树外重新配置、构建
  模块并由安装后的 `gneiss_runtime` 完成 smoke 运行。
- 收敛 Engine Library、Runtime 宿主、Game Module 与 Editor 的外部消费和兼容边界；新增能力继续
  保持 Experimental，不在 0.30.0 提前冻结 1.x ABI。

## 0.29.0 - 2026-09-06

- 规划 0.29.0 Granit 异步 GPU 契约接入：审计异步操作生命周期，并让 RenderPipeline 指标层
  复用跨后端异步 Timestamp 能力。
- 将 Granit 依赖升级到 0.14.0，PACKAGE、安装消费和旧默认缓存迁移同步采用 0.14 基线。
- GPU 性能统计改为报告 Granit 的独立样本序列，不再用待处理 Frame 队列猜测跳采样后的来源帧。

## 0.28.0 - 2026-09-06

- 规划 0.28.0 Granit 0.12 资产契约收敛：接入公共 Shader Asset 检查、标准 PBR Schema/Material
  和统一 RenderPipeline 资产目录，删除 0.27.0 的临时重复实现。
- 将 Granit 依赖升级到 0.12.0，PACKAGE、安装消费和旧默认缓存迁移同步采用 0.12 基线。
- PBR Shader Resolver 使用 Granit 公共元数据检查，标准 Material、参数、Binding 与顶点布局改以
  Granit 公开契约为唯一来源；删除仓库内同构 PBR 归档和源码目录推导逻辑。
- 修复 Windows 静态构建冷启动时停止协议测试过早发送信号的竞态，测试改为等待 Runtime 完成启动。

## 0.27.0 - 2026-09-06

- 明确 Gneiss 作者资产与 Granit 后端投影边界，Granit 专用 Shader、Material 和 Environment 格式
  仅作为可失效的派生或安装数据，不进入 Scene、公共 API 和 IPC。
- 渲染线程按 Frame Slot 异步采集整帧 GPU 时间戳，并以 Frame 序列关联延迟结果；不支持或暂不可读
  时保持正常渲染，退出诊断报告整帧及阴影、不透明、色调映射阶段时间。
- Granit Render Pipeline、Mesh 和 Material Instance 接管后端投影；Material v3 与 glTF 导入器增加
  基础颜色、金属度和粗糙度映射，并内嵌标准 PBR 后端资产。
- 工程格式 v3 支持单环境资产、强度和水平旋转；未配置时使用内建中性环境，无效载荷回退内建环境。
- 将 Granit 依赖升级至 0.10.0，Fetch 锁定正式发布提交，PACKAGE 与安装消费最低版本同步提升
  至 0.10。

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
