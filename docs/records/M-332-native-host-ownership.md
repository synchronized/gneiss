<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-332：原生宿主拥有 Application

2026-10-16，按已确认方案 A 接入 Editor、Runtime 和 Project Manager。
三者直接创建 engine::application，并直接调用主循环、获取 World 和装配任务执行器。
配置使用内部窗口和环境字段，不再为创建拼装 C 描述或使用 SDK 拥有包装。
原生创建校验窗口和环境参数；失败保留原输出，未进入初始化回调。

尚未迁移的场景、资产、UI 及 Game Context 接口通过内部 application_bridge 借用句柄访问
同一个对象。桥接先于宿主对象析构；请求/模块清理仍在对象和任务执行器存活期间执行。
这不代表所有模块的 C 入口已移除：本轮回调签名和结果码仍使用原 ABI 表达，待继续收敛。
Editor 调试绘制仍使用 SDK 借用操作，但不参与 Application 的创建、拥有和关闭。

本地 Windows Clang Debug 构建成功，7 项相关回归通过：native-application、c_application、
cpp_application、runtime.smoke、runtime.startup-diagnostic、editor.smoke、
editor.project-manager-smoke。原生测试额外覆盖无效环境参数失败不替换输出。
未执行 Linux、静态安装矩阵或完整 Sponza，不将启动测试等同大型场景验收。

## 原生日志回调边界

Application 配置现在直接持有已有的 `log_internal::event_sink`，投递器消费原生事件。
C 日志布局转换移至 `engine/api/application_log_sink.hpp`；ABI 配置适配分配失败转换为结果码。
Runtime 使用捕获上下文的原生订阅，现有日志/IPC 消费接口的 C 值结构转换暂留在 Runtime 边界。
未新增日志队列、线程或第二套运行时状态。

Windows Clang Debug 相关目标构建通过，15 项日志、Application、IPC 和启动回归通过。
原生测试覆盖投递内容、序号、回调异常隔离及关闭前排空。其他生命周期回调尚待迁移。

## 原生平台回调边界

初始化、事件轮询和关闭改用 C++ callable；初始化和轮询使用已有强类型 `result`，
关闭标记使用 `bool&`。C 函数指针、上下文及 `uint8_t` 转换只保留在 ABI 创建入口。
配置复制 callable，不新增宿主状态；捕获的借用对象须保持到关闭完成。

回调抛出的分配异常映射为 out_of_memory，其他异常映射为 internal。
失败初始化仍配对清理；关闭回调失败仍释放后端，且不会在析构时重复调用关闭回调。
原生测试覆盖初始化异常、轮询关闭、轮询异常和关闭异常；15 项相关回归通过。

剩余：update、diagnostic、close_requested、时钟与主循环的句柄参数仍是兼容路径，
Application 整体迁移尚未完成。本轮未验证 Linux 或静态安装矩阵。

## 主循环回调与时间视图

2026-10-16，update、diagnostic、close_requested 和时钟改为原生 callable，配置移除 void*。
主循环、Editor/ImGui 与 Runtime 固定更新调度器使用内部 frame_time；诊断使用枚举及借用文本。
C/SDK 回调的原布局在 API 入口逐字段构造，原生宿主通过捕获保存既有上下文。
回调中的关联 ID 暂供尚未迁移的宿主调用借用桥，未新增拥有权；不是句柄桥整体移除。

时钟、更新及关闭请求抛异常时，run 清理更新状态与临时绘制列表，释放运行标志后返回结果码；
诊断接收方异常不会打断初始化/关闭及日志投递。原生测试验证时钟失败后再次运行、更新/关闭
请求失败后正常关闭、诊断异常不覆盖原始初始化结果。
完整 Windows Debug 构建及 11 项 Application、日志、Runtime 调度、宿主和窗口回归通过；
诊断枚举进一步收敛后，4 项 C/SDK/原生 Application 与 C Scene 回归通过。

剩余：宿主借用桥、UI 输入和字体资源链、场景/资产调用仍需继续迁移。未执行最终安装与跨平台矩阵。

## UI 原生链与无桥 Project Manager

ImGui 的键鼠状态、事件、窗口尺寸、字体纹理创建销毁与绘制提交均改用原生接口，
入口检查所属线程与关闭状态。Render 仅为同源码 UI 导出所需纹理创建/销毁方法，私有头不安装。
键码与修饰位编号仍沿用现有公共常量，不再使用 C 输入事件、C 纹理描述或 C 函数调用。
Project Manager 和窗口交互测试移除借用桥；Project Manager 直接调用 application 请求退出。
Editor 主窗口仍为场景/资产等剩余链路保留桥接。

完整 Windows Debug 构建及 7 项 UI、资源、窗口交互、宿主冒烟和边界回归通过。
追加窗口测试的跨线程 begin_frame 拒绝验证；最终安装、Linux 和真实跨屏验证仍待执行。

## 宿主资产请求与退出链

Editor 的视口提交、冒烟统计和退出，Runtime 的资产请求、完成轮询、取消、进度和首帧日志
直接调用借用的原生 Application。动态模块及剩余场景接口仍保留必要的兼容句柄。
原生资产入口拒绝跨线程和关闭后请求；失败时清零请求/ready，不改写未返回的完成对象。

完整 Windows Clang Debug 构建通过，9 项原生/C/SDK Application、纹理加载、Runtime 资产/IPC
及 Editor/Runtime 启动回归通过。新增原生测试覆盖空闲取消、跨线程及关闭后的资产入口。
此项不代表 Scene/World 和最终安装矩阵已完成。

## Runtime 原生场景协调

Runtime 场景加载协调器改为借用 Application 引用，直接请求、取消、轮询和激活场景，
不再通过注册表调用场景加载兼容函数。宿主先销毁协调器，再销毁 Application；模块激活前后的
安全点回调保持原有顺序。原生场景入口补齐所属线程、关闭状态及异常到结果码的转换。

完整 Windows Clang Debug 构建通过，8 项 Application、场景加载、Runtime 启动、Editor 远端场景
加载及灯笼场景 Runtime 工作流通过。原生入口增加错误线程和关闭后场景操作拒绝测试。
Editor 的编辑会话、场景实例操作和游戏模块 Context 仍含兼容调用，尚未整体移除宿主桥。

## 后续拥有链审计与异常边界补齐

直接调用原生资产请求/轮询后，原有兼容函数中的异常转换不会再被执行；已将对应转换补入
原生入口，避免分配异常越过 Runtime 的资产请求结果协议。完整 Debug 构建和 5 项资产、原生
Application、Runtime 启动及远端场景加载回归通过。

下一步的具体入口：

- `src/editor/editor_session.hpp` 仍通过 SDK scene_instance 拥有编辑场景；须将编辑会话改为
  对原生场景域的受控借用，并保留关闭、场景切换及失败采用时的有效期语义。
- `application_scene_state` 当前保存 World RID；`world_service.cpp` 的全局表实际拥有
  unique_ptr<world_state>。原生拥有链迁移需一起调整注册表与 C 创建/销毁适配，不能仅替换类型名。
- Application 返回的 World 已明确为借用句柄，迁移不得赋予调用方销毁场景域的权限。
- Editor 的场景加载协调器须随编辑会话迁移；Runtime 的模块 Context 仍是独立动态模块的真实
  ABI 边界，不能因去除内部兼容调用而删除该边界。

以上仍为未完成项；本次未执行最终静态安装、Linux 或完整 Sponza 验收。

## 场景域直接拥有 World

场景域改为持有原生 World 的唯一拥有指针，C 注册表对该对象仅保存借用入口。
自定义删除器留在 World 模块，自动撤销 RID 后释放对象，不向 Application 传播 EnTT 头。
独立 C/SDK World 仍由注册表拥有；对 Application 借用 World 调用 destroy 拒绝并保留对象。
这落实既有独占拥有契约，不改变 C ABI 布局、符号和场景切换时的退休回收顺序。

完整 Windows Clang Debug 构建及 12 项 World、Scene、Application、SDK 生命周期、场景构造和
编辑器/Runtime 场景工作流通过。新增测试覆盖移动、失败创建保持拥有者、借用误销毁、错误线程、
析构后失效及槽位复用；World 的操作接口和组件值仍待继续迁移。

## 原生 World 渲染组件

ECS 相机组件复用原生 camera_settings；网格渲染组件只保存原生资源 RID。活动相机仅维护
World 的实体 ID，移除每个相机组件重复存储的 is_primary 位。Scene/Prefab 的相机与网格绑定
也改用原生值，C 布局只在 world_api 转换。保留原有参数校验及主相机选择语义。

完整 Windows Debug 构建和 13 项 World、Scene/Prefab 热重载、纹理重载、快照及正反向边界
测试通过。新增边界规则拒绝 World 原生组件重新依赖 C 渲染结构。此前组件首批的 11 项回归
包含反射、属性访问和编辑器相机，也已通过。

## 场景树原生 TRS

场景树的节点存储、局部/全局变换及父子组合统一使用 engine::transform。World 到 Render
快照直接复制同一原生值，移除快照生产端的 C TRS 转换。旧 World 服务的兼容入口暂保留
逐字段转换，失败查询仍保持原有输出约定；这些兼容入口尚未视为原生 World 操作 API 完成。

完整 Windows Debug 构建及 13 项变换、World、快照、Prefab、Scene 热重载和边界回归通过；
新增场景树 C TRS 反例后，两项正反向边界测试再次通过。迁移仍未覆盖 Editor 编辑会话、
全部 World/Scene 操作入口和最终跨平台安装矩阵。

## World 原生操作对象

World 操作实现迁入 engine::world，内部独占 ECS/场景树实现，宿主头不包含 EnTT。
创建仍由统一工厂分配域；构造函数不对内部任意调用方开放。兼容 World 服务集中做句柄解析、
校验与边界值转换，再调用同一对象，未新增业务状态。

原生变换操作使用 engine::transform，存活查询使用 bool；Application 每帧及截图快照直接
调用活动域的 World，不经过全局句柄表。场景切换前后的借用不得跨激活保存。
候选构造描述和 Scene/Prefab 消费者仍需继续迁移，不能据此宣布整个 World/Scene 链完成。

完整 Windows Debug 构建及 19 项 World、Scene、Application、窗口交互、反射和边界回归通过。
新增测试直接调用原生对象，验证实体、节点、变换、跨线程拒绝、候选发布和 C 查询共享同一状态。
兼容解析集中化后，7 项受影响回归再次通过。最终跨平台与安装矩阵尚未执行。

## World 候选构造值边界

候选节点构造描述改用原生 TRS，World 类内部不再转换 C Transform。Prefab 的现有描述在
进入该原生入口前逐字段适配。边界检查扩展到 world.hpp/cpp 与 world_types.hpp。
完整 Windows Debug 构建及 7 项候选构造、场景加载、Prefab 与边界回归通过。

反射路径核对 ADR-066 后继续保留旧运行时 setter 的渐进适配与唯一注册表，不另起反射事务协议。
这不是新增待批准决策，也不表示 Scene/Prefab 的其余句柄调用已经清除。

## Scene/Prefab 直接借用原生 World

Scene 服务、场景实例和 Prefab 投影借用场景域拥有的 engine::world；普通实体、节点、相机、
渲染组件和候选发布操作直接调用对象，不再进入 World 全局句柄表。保留的 reflection_context
仅供既有 ABI 反射回调定位，不负责所有权；跨实例候选校验使用原生 World 身份。

场景创建描述、查询结果、Prefab 构造与回滚快照统一使用 engine::transform。C API 在边界逐字段
转换；原生反射候选 Transform setter 同步迁移，旧运行时反射 setter 保留显式转换。
World 必须比所有场景和 Prefab 投影活得更久；Application 场景域保持先释放场景再释放 World。

Windows Clang Debug 完整构建及 21 项相关回归通过，覆盖 C/C++ Scene、World、候选构造、
取消、结构重载、编辑器 Prefab 操作及反射。Editor 编辑会话仍使用 SDK 场景包装，继续迁移；
最终安装和跨平台矩阵尚未执行。

## Editor 原生编辑会话与异步场景加载

editor_session 接收原生 Application，保存场景标识并通过当前场景服务执行编辑，不再持有 SDK
scene_instance 或调用 World/Scene C 入口。查询缓存仍使用现有编辑器值类型，尚未将这些值包装
全部移出 SDK。会话只借用 Application，宿主须先关闭会话和历史命令再关闭 Application。

会话通过 World 域标识检查是否仍属于活动域，不跨激活缓存服务指针。移动会话转移唯一卸载责任；
接管失败保留原文档和调用方场景；关闭时已经退休的旧域由 Application 回收。异步场景加载器的
请求、进度、取消和激活直接调用原生 Application；宿主接入同步更新。

测试中的 C Application fixture 通过私有注册表借用同一个原生对象，以保留 C/原生互操作覆盖；
该解析符号仅供同源码构建，不进入安装公共头。Editor 目标的内部包含路径只对构建树开放。
Windows Debug 完整构建与 13 项编辑器行为回归通过；新增会话移动、外线程拒绝、无效接管保留
文档测试及边界正反例共 4 项通过。资产编辑和 Prefab 放置事务的 Application 兼容入口仍待迁移。

## Editor 资产事务与 Prefab 原生接入

资产就绪查询、候选准备、绑定捕获/替换及未使用租约回收由 Application 原生入口协调原有服务。
兼容函数仅解析句柄后委托，不保留第二份实现。原生入口保留输出重置、所属线程、关闭状态与
异常转换；新增测试验证外线程及关闭后的拒绝行为，接受回调在拒绝时不能执行。

Editor 资产编辑、Prefab 描述准备、放置/刷新及历史操作借用原生 Application。异步任务仍只持有
VFS 副本和结果；历史仍持有实际资产租约。场景事务、历史及会话必须在 Application 之前清理，
不得把隐藏候选带过场景域回收。Editor 主程序移除 Application 兼容桥，观察相机聚焦直接读取
原生 World；动态模块协议没有改变。

Windows Clang Debug 完整构建与 15 项相关回归通过，包含原生入口、Prefab 准备、编辑器资产/
Prefab 编辑、候选构造、取消、结构重载、窗口交互及两项灯笼工作流。Runtime 检查/统计中的
兼容调用、SDK 命名收敛与最终安装矩阵继续推进，不能据此标记整个迁移完成。

## Runtime 场景消费入口

Runtime 的结构重载、节点统计、实体统计、场景卸载及检查快照改用原生 Application/World/Scene。
检查快照在 IPC 输出边界转换为既有 Transform/Camera 布局，协议、对象身份和增量语义保持不变。
新增外线程捕获拒绝测试，失败保持已有快照。动态游戏模块仍通过真实 C ABI 桥接；既有反射回调
按 ADR-066 保留，不将这些外部协议边界误报为普通场景操作。

Windows Debug 构建及 7 项 Runtime/Editor 联调回归通过，包括检查、属性编辑、资产重载、IPC、
Runtime smoke、场景切换和灯笼运行流程。

Gizmo 显示与拖动的 World 查询也已归入原生编辑会话，不再传入 SDK world_ref 或独立 World
句柄。Windows Debug 构建及会话、Gizmo 拖动/显示 3 项回归通过。

## SDK 名称与安装消费验证

World、Scene、Reflection、Render 资源所有者及 Game Module 包装统一进入 `gneiss::sdk`，
公共值类型保持原命名空间；不提供旧所有者别名。示例、测试、模板和 Reference 同步更新，
迁移方法见 [0.50 → 0.51 指南](../guides/migrating-0.50-to-0.51.md)。

Windows Clang Debug 共享与静态配置完整构建通过。共享库 20 项普通 SDK/ABI 回归、
两项终止行为测试、独立安装的 4 个 C/C++ 消费者及 stable runtime 消费者通过。
终止测试已确认进入 terminate handler；Windows 测试改用 TerminateProcess 保持退出码断言，
避免进程卸载 DLL 时再清理仍存活的资源干扰此断言；非 Windows 保持 _Exit。
该调整只涉及死亡测试，不代表生产关闭路径问题已被修复或豁免。
静态安装、最终平台矩阵及完整场景仍待验收。

## 属性检查器原生所有权

Editor 与 Runtime 属性检查器直接持有原生反射注册表，通过内部工厂创建同一实现，
不再经 SDK 或公共 RID 表创建和访问。公共 C 创建仍复用相同工厂；回调协议、World 上下文
与字段布局按 ADR-066 保留。编辑模型保持不可复制，不新增第二份运行时状态。

完整 Windows Debug 构建通过；属性检查器、属性编辑/面板、Runtime 属性命令、反射边界及
边界正反例共 7 项通过。新增原生注册表冻结前拒绝读取、冻结后读取、最后所有者释放和
空引用拒绝测试。静态配置独立安装的 4 个 C/C++ 消费者及 stable runtime 已通过
（SDK 命名提交；本次反射内部修改后的静态增量回归另行执行）。

## 整体回归与复用纹理上传修复

2026-10-17，Windows 共享 Debug 的 Gneiss 非安装测试共 201 项，首轮 200 项通过。
资产工作流首轮启动等待失败，单独复现进一步发现材质重导入的上传回执返回 invalid_state：
固定版本纹理被复用而不重复上传，后端却只查询准备表；首次绘制后镜像已经移到活动表。

Gneiss 的 Granit 适配现在先查询准备表，再以材质纹理 RID 和数据身份查找活动镜像，
避免复用正确版本时误报缺失，并继续拒绝错误身份。未修改 Granit。资产暂存、提交、回执和
发布失败补充阶段诊断。原失败工作流及 6 项纹理加载、GPU 像素/负缩放和 Runtime 回归通过，
未放宽超时或关闭原测试。两项依赖正反例通过；新增规则禁止 Editor 引入 SDK 所有者和
Registry C 入口。完整场景、跨平台及最终验收仍单独记录。

## 安装 Runtime 与静态回归

静态 Debug 增量完整构建及 26 项 SDK/原生/反射/依赖回归通过。共享安装 Runtime 验收发现
首帧原生日志未携带旧适配层填入的 application 来源；已在调用点显式保留该字段，未修改日志
协议或放宽安装测试。修复后共享安装 Runtime、独立游戏模板构建和打包启动验收通过。

完整 Sponza Release 单次功能样本成功，553 节点、505 资源，加载约 275.663 秒，主循环最大
74.188 ms，P95 18.896 ms；GPU 回读图像已人工检查。这一轮只作为功能/预热样本，不能代替
计划规定的重构前后同机三次性能对照。原始输出保存在本地 build/migration-051-full-scene-20261017。
