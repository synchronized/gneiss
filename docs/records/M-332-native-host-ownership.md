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
