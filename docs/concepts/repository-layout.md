<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 源码目录与模块所有权

## 目的

本文描述当前实际源码布局，不把已批准的迁移目标当成已实现。0.45 的精简目标目录与宿主边界见
[ADR-054](../decisions/ADR-054-source-layout-and-host-boundaries.md)，迁移映射与验收见
[DEV-045](../plans/DEV-045-cpp-boundaries.md)。Core、Platform、Resource 与 Function 已迁入 `src/engine/`，
基础 C ABI 适配位于 `src/engine/api/`，应用相关适配位于 `src/runtime/api/`，`src/editor/` 管理 UI、资产服务、编辑模型、场景会话与创作事务；
`apps/` 保持位于仓库根目录，仅保留可执行入口；宿主装配与协议适配位于 `src/host/`。

总体分层以[总体架构](architecture.md)为准，代码与文档规范分别以
[C/C++ 代码风格](../guides/coding-style.md)和[项目文档规范](../../DOCUMENTATION_GUIDE.md)为准。

## 当前资源模块

资产管理器、资源描述、解析、VFS 及 Prefab Loader 已统一位于
`src/engine/resource/`；旧 `src/engine/asset/`、`src/engine/function/asset/` 已删除，
没有兼容转发头。渲染 Loader 与上传保留在 `function/render`，Prefab 实例化和编辑保留在
`function/scene`。资源代码已按 `types/`、`loaders/`、`vfs/` 和 `cache/` 分类。
Application 组装位于 `src/runtime/`，加载流程位于其 `loading/`，配置位于 `configuration/`。
游戏上下文归 `runtime/game/`，依赖 Application 的 C 适配归 `runtime/api/`。
Engine 源码不得反向包含 Runtime；当前仍一起编入 `gneiss_engine`，没有新增公共库目标。
目标与剩余批次见[模块布局计划](../plans/PLAN-051-module-layout.md)。

## 当前顶层目录

| 目录 | 所有权与职责 |
| --- | --- |
| `include/gneiss/` | 仅根总入口；Engine 公共声明和包装位于 `engine/` |
| `src/` | 内部运行时实现、第三方适配及离线处理 |
| `apps/` | Editor、Runtime、project、assetc 的薄程序入口 |
| `src/host/` | 宿主启动、项目工具、进程与 IPC 协议适配 |
| `scripts/` | 仓库维护与性能测量脚本 |
| `tests/` | 公共接口、内部行为、生命周期与集成验证 |
| `examples/` | 使用公共接口构建的独立最小示例 |
| `docs/` | Guide、Reference、Concept、Plan、ADR 和执行记录 |
| `3rd/` | 锁定版本并与自有代码隔离的第三方依赖 |
| `cmake/` | 项目构建策略和可复用的 CMake 模块 |

当前 `gneiss_engine` 的四层实现已集中在 `src/engine/`，基础 C ABI 适配位于 `src/engine/api/`，应用相关适配位于 `src/runtime/api/`。
Runtime 与 Editor 宿主分别位于 `src/host/runtime/` 和 `src/host/editor/`；两者可以依赖 Engine Library，
Engine Library 不得反向依赖它们。Editor 的 ImGui Context、字体、主题、DockSpace 和通用控件由
`src/editor/` 中不安装、不导出的 `gneiss_editor_ui` 内部静态库统一管理，面板选择状态仍由 Editor 应用层持有。
控制台面板由 `console_panel.hpp/.cpp` 绘制，借用日志模型及进程展示值；清空请求通过宿主同步回调执行。
作者层级树由 `author_hierarchy_panel.hpp/.cpp` 读取节点快照，拖拽和菜单请求在整棵树绘制后处理，
避免递归期间刷新节点数组。
作者属性 Inspector 由 `author_property_panel.hpp/.cpp` 绘制，属性写入与撤销事务归
`author_property_edit.hpp/.cpp` 所在的会话目标；Host 只提供同步连接，不把应用状态交给 UI 实现。
Runtime 层级树和属性面板由 `runtime_panels.hpp/.cpp` 绘制，只读取 Editor 模型；
Host 在每次绘制时提供同步属性提交、回写作者场景与结果报告回调，协议值只在宿主边界适配。

编辑器资产浏览、监听、导入控制、重导入队列与后台服务也位于 `src/editor/`，
由 `gneiss_editor_assets` 内部静态库组织，依赖 Engine、Tooling、任务调度和平台 IO。
该目标不依赖 Host 或编辑会话；入口仍在 Host 装配服务并轮询结果。
资产面板位于 UI 目标，仅借用资产模型和单帧显示值；Host 适配协议与进程状态。面板返回拥有
选择身份的请求，宿主在绘制结束后执行，避免绘制期间的服务变化使借用输入失效。

`gneiss_editor_model` 位于 `src/editor/`，管理相机、网格、旋转/Gizmo 数学、命令历史、属性检查
及创作事务、控制台模型，不依赖 Host 工程、IPC 或会话。场景会话与 Gizmo 拖拽由同目录的
`gneiss_scene_document` 管理，只依赖该模型库与 Engine。`src/host/editor/` 的 `gneiss_editor_host`
负责工程、子进程、IPC 与运行时同步装配。
Gizmo 绘制和 ImGuizmo 适配归 `transform_gizmo_view.hpp/.cpp`，只借用会话、历史与拖拽状态。
`apps/editor/main.cpp` 仅转交 `run_editor_application`；`editor_application.cpp` 拥有窗口与程序状态，
组合项目菜单、文件对话框、服务轮询和面板请求。退出时先清理命令历史，再关闭其借用的会话。

`src/host/common/` 保留两个宿主私有目标：`gneiss_app_project` 管理构建/启动工程描述及启动日志协议，
`gneiss_app_ipc_protocol` 管理 Editor/Runtime 进程通信协议。它们不安装、不导出为 Engine SDK；
Runtime 直接使用这些共用契约，不链接 Editor。通用 IPC 传输和子进程机制由 Engine Platform 提供，
工程格式、消息领域与启动约定继续归 Host。Host 私有 C++ 头统一使用 `.hpp`，不属于公共 C ABI。

`asset_scene_commands.hpp/.cpp` 属于 Editor 会话目标，统一添加 Mesh/Prefab 与替换资源的撤销事务；
它只接收作者身份和资产 URI，不依赖资产面板、导入服务或宿主协议。

`src/editor/runtime_author_apply.hpp` 接收 UUID 与局部变换视图，负责变换回写和撤销命令。
Host 从 IPC 节点提取输入；Editor 命令复制身份字符串，不保留协议对象或借用字符串。
控制台接收拥有字符串的 `console_event`；Host 的日志适配负责从已解析协议记录移动字段，
控制台不保存协议版本。属性编辑状态也已归入独立模型，使用 Editor 自有请求、结果与类型化属性值；Host 适配 IPC，
模型继续唯一管理 pending、乱序响应、超时和断连。场景镜像同样位于独立模型库，
使用 `runtime_scene_data.hpp` 的节点批次，唯一管理分块、顺序与图校验。宿主只适配解码结果，
镜像与属性模型共用 `runtime_object_id.hpp`；不向 Runtime 传播 Editor 库依赖。
运行前的场景保存与启动条件判断由 `src/editor/runtime_launch.hpp` 提供；进程启动仍归 Host。

离线导入库的构建定义与 C++ 实现集中于 `src/tooling/`，内部头使用 `.hpp`。
assetc CLI 实现位于 `src/host/assetc/`，入口位于 `apps/assetc/`，工具测试由 `tests/tooling/` 组织，性能脚本位于 `scripts/performance/`。

公共头目录按能力组织，`.h` 提供 C ABI，部分 `.hpp` 提供共享值类型与无状态转换。内部源码目录按拥有运行时状态的模块组织，
不机械复制公共头目录。

## 运行时模块

以下为当前实现路径；按各模块实际迁移结果更新。

| 模块 | 目录 | 职责 | 状态 |
| --- | --- | --- | --- |
| Core | `src/engine/core/` | 结果、RID、Service 注册等无领域倾向的基础设施 | 已存在 |
| World | `src/engine/function/world/` | World、Entity、System 与 EnTT 适配 | 已存在 |
| Scene | `src/engine/function/scene/` | Scene Tree、节点映射与层级 Transform | 已存在 |
| Application | `src/runtime/` | 顶层生命周期、初始化回滚和主循环编排 | 已存在 |
| Platform | `src/engine/platform/` | 窗口、IO、IPC、子进程和动态库等平台能力的隔离 | 已存在 |
| Asset | `src/engine/resource/` | VFS、格式、CPU 解码与缓存 | 已迁移 |
| Render | `src/engine/function/render/` | 渲染输入快照、资源 RID、帧包与执行器 | 已存在 |
| Input | `src/engine/function/input/` | 输入状态与动作映射 | 已迁移 |
| Game | `src/runtime/game/` | Game Module 上下文与宿主桥接 | 已迁移 |
| Reflection | `src/engine/core/reflection/` | 唯一类型注册表与属性访问 | 已存在 |
| Log | `src/engine/core/log/` | 通用文本校验与异步事件投递；Application 独立适配公共回调 | 已迁移 |
| C ABI | `src/engine/api/`、`src/runtime/api/` | 公开描述适配、异常隔离及内部入口委托 | 已存在 |
| Tooling | `src/tooling/` | 离线资产处理实现 | 已存在 |
| Granit 后端 | `src/engine/function/render/backend/granit/` | Granit 类型、调用和错误转换的隔离 | 已存在 |

Platform 的 Granit Window 适配位于 `src/engine/platform/granit/`；`src/engine/function/render/backend/granit/` 只负责
渲染后端，两者不共享原生对象所有权。窗口配置与原生窗口视图归 `gneiss::platform`；
Application 的 C 入口将版本化描述转换为内部 `application_configuration`，Platform 只接收
`window_configuration`。用户回调仍是实际 ABI 边界，不将回调上下文复制成另一套状态。渲染快照的值类型位于 `src/engine/function/render/render_snapshot.hpp`，
World 的提取函数位于 `src/engine/function/world/render_snapshot.hpp`，Render 不反向包含 World/Scene/Application。
`src/engine/resource/loaders/png_decoder.hpp` 提供 CPU 解码，Cook 与运行时加载共用；解码不依赖渲染服务。

Scene 的节点创建使用内部借用参数，普通节点与 Prefab 查询返回内部视图。公共查询结构的版本写回
归 `src/engine/api/scene_query_conversion.hpp`；核心查询不依据 ABI 尺寸分支。字符串寿命仍由 Scene 管理。

纹理加载与上传事务由 `src/engine/function/render/texture_load_service.hpp` 组织，属于 Render 功能层；
其依赖 Asset 的 VFS，但 Asset 不反向包含 Render。

Render 的同步资源创建接收内部 `mesh_view`、`texture_view` 与 `material_data`，借用数组只在
创建调用期间有效。公共描述的尺寸、版本、保留字段与指针/长度转换归 `src/engine/api/`；资源服务
继续负责数值、拓扑、RID 与预算校验，成功后拥有数据副本。资产加载器不生成版本化 C 描述。

`render_asset_loader` 暂仍位于 Render，它连接 Asset 缓存租约与 Render 资源发布，并不只是格式
解码器。CPU 准备和解析实现位于 `src/engine/resource/loaders/asset_preparation.hpp/.cpp`，
Render 的准备适配只传入发布资源的预算。三类准备数据均使用 Asset 值类型，材质依赖仅用 URI，
发布边界绑定纹理 RID 并移动纹理数组；上传弱引用仍由 Render 管理。Asset 不包含 Render 实现，
独立准备测试不链接 Engine、缓存或资源服务。其余迁移见 [0.45 实施计划](../plans/DEV-045-cpp-boundaries.md)。

当前测试分布在 tests 根目录及 `tests/core/`、`tests/world/`、`tests/scene/` 等模块目录。
公共头独立编译位于 `tests/headers/`，最小游戏模块安装消费者由
`cmake/check_installed_game_module.cmake` 使用 `templates/game/` 验证；完整 SDK 安装消费者已撤销。
跨模块工作流还通过 CMake 脚本验证。尚未统一迁移到新的测试层级，不创建空的 integration 目录。

## 演进规则

- 新目录必须对应一个已经开始实施的模块或工具，不添加占位文件。
- 新模块先确认职责、拥有的状态、公开接口和允许依赖；跨层变化使用 ADR 记录。
- 第三方后端放在所属 Service 下面，不建立可被所有模块随意依赖的通用 `backend/`。
- 平台差异集中在 `src/engine/platform/` 或具体后端目录，不散布到 World、Scene 和业务组件。
- 单个实现文件只被一个模块使用时留在该模块内部；只有形成稳定跨模块契约后才提升为公共接口。
- 目录调整应伴随真实代码迁移和验证，不单独进行大规模结构美化。

## 公共头路径

Engine 公共接口统一位于 `include/gneiss/engine/`，基础类型位于其 `core/` 子目录。
例如使用 `<gneiss/engine/scene.h>` 或 `<gneiss/engine/scene.hpp>`；根总入口
`<gneiss/gneiss.h>` 保留，C++ 对象总入口已删除。旧功能头和 `gneiss/core/` 转发路径已删除，
仅安装规范路径与 C 总入口；C 符号和运行时状态不因此改变。升级时使用干净安装前缀，
避免旧安装留下的转发头继续被编译器找到。

公共目录提供 Engine C ABI 与共享值类型；不建立 Runtime、Editor、Tooling 或插件公共 API，
不改变内部模块边界。现有 `apps/runtime` 仍是宿主，而非第二套引擎库。

## 原生 C++、C ABI 与 SDK 的阅读顺序

同源码宿主从内部模块接口进入，外部使用者从公共接口进入；两条路径操作同一份状态。
公共对象 SDK 已删除，`include/gneiss/engine/*.hpp` 仅保留值类型和无状态辅助。
公共值类型仍位于 `gneiss`；内部模块命名空间与文件路径一起标识职责，不能仅凭同名判断所有权。

| 要了解的行为 | 入口 | 拥有或借用关系 |
| --- | --- | --- |
| 程序启动与关闭 | `src/host/editor/editor_application.cpp`、`src/host/runtime/runtime_application.cpp` | 宿主拥有原生 Application，先清理依赖它的会话、命令与任务 |
| Application 装配 | `src/runtime/application.hpp` | 拥有平台、服务与场景协调组件；普通内部操作不解析自身 C 句柄 |
| 场景切换 | `src/runtime/loading/scene_service.hpp` | 拥有活动与退役域、内部加载任务，协调激活 |
| 加载实现 | `src/runtime/loading/scene_service_loading.cpp` | 统一服务的内部临时任务；后台准备、同步实例化与激活 |
| 场景域 | `src/runtime/loading/scene_domain.hpp` | 仅拥有 World 与场景实例集合；不重复存储资源加载器引用或 World RID，初始化成功才发布 |
| World 数据和操作 | `src/engine/function/world/world.hpp` | 原生对象拥有 ECS/树状态；`world_factory.cpp` 创建独占拥有者，反射直接借用对象 |
| Scene 实例集合 | `src/engine/function/scene/scene_instances.hpp` | 通过强类型节点 ID 操作实例；借用所属 World |
| Scene 私有实例 | `src/engine/function/scene/scene_instance.hpp` | 维护节点、作者描述及所使用资源版本的租约，不向宿主提供头文件 |
| Prefab 实例操作 | `src/engine/function/scene/scene_instances.hpp` | 消费已准备资源同步创建；直接删除和替换，不使用隐藏候选或发布组；见 [ADR-088](../decisions/ADR-088-prefab-direct-instantiation.md) |
| 作者文档 | `src/editor/documents/scene_document.hpp` | 借用 Application、拥有文档的场景实例 ID；关闭时卸载，不能晚于 Application 销毁 |
| 反射属性编辑 | `src/editor/panels/property_inspector_model.hpp`、`src/host/runtime/runtime_property_editor.hpp` | 直接持有同一内部 Registry 实现；字段回调仍使用已定义的目标协议 |
| 外部调用 | `src/engine/api/`、`include/gneiss/engine/` | C 适配转换参数并委托，公共 C++ 头仅提供值类型与转换 |

Editor 无需 Application 兼容桥；Runtime 仅在动态游戏模块这一真实 ABI 边界保留借用桥。
借用桥不授予关闭权，Application 返回的 World 句柄也不能由外部销毁。场景域退役后其旧句柄失效。
内部会话和事务的借用有效期由宿主销毁顺序保证，不把普通引用宣传成可跨 Application 存活的句柄。

加载链路按 Application → scene_service → 候选场景域 → Scene/World → 渲染快照阅读；
共享资产状态与场景域生命周期不同，详见 [资产与场景域所有权](architecture.md#资产与场景域所有权)。
原生渲染值、输入状态和日志事件均在各自模块定义，C 布局转换归边界。无状态解析、哈希与数学
仍使用自由函数；有状态对象持有实际状态，不为消除自由函数增加转发 Manager。

上述是当前实现入口，不代表 0.51 的大型场景与平台验收已经完成。
外部源码迁移见 [0.50 → 0.51 指南](../guides/migrating-0.50-to-0.51.md)。

## 同源码内部接口的可见范围

- `cmake/internal_interfaces.cmake` 是宿主与离线工具获准头文件清单的权威来源；
  `gneiss_native_interface`、`gneiss_tooling_interface` 只提供构建期契约，不属于安装 SDK。
- Task 目标独立导出 `task_scheduler.hpp` 与进度通知，不能通过其依赖获取 Engine 功能层。
- `cmake/internal_headers.cmake` 在构建树生成清单限定的包含目录；消费者通过 target 依赖获得它。
  新增跨模块头依赖时须更新清单并审查方向，不能恢复 PUBLIC `src` 或相对路径越过模块根。
- Engine 与资产工具编译自身实现时保留 PRIVATE 源码路径。白盒测试显式申请私有路径；
  普通宿主的可见性测试不获得此权限。
- `scene_load_types.hpp` 仅包含加载进度与完成信息；`scene_service.hpp`、场景域状态及
  注册表实现不向宿主导出。动态游戏模块所需桥接通过 `application_bridge.hpp` 提供。

属性面板和撤销历史使用 `reflection_internal::property_value`，其中字符串为自有文本；
`property_registry.hpp` 提供同一 Registry 的原生查询和写入。内置组件注册入口是
`world_reflection.hpp`，无需包含 World 句柄表。公共 SDK 值适配位于 `detail/reflection_values.hpp`，
不携带 SDK 所有者；外部借用文本与内部自有快照的生命周期分别遵循各自接口约定。

`scene_instance_edit.cpp` 实现单实例的已就绪材质绑定提交；Scene 服务校验入口，实例同步
更新 ECS、作者描述和资源租约。`scene_mesh_binding.hpp` 与 `render_statistics.hpp` 分别提供
绑定租约值及渲染统计快照，Application 不通过它们传递包含 Scene 服务或渲染执行器定义。
需要调用 Scene/World 服务的宿主源码显式包含对应契约；Application 的纹理加载器和内部
资产加载器访问方法为私有，兼容入口通过 Application 请求/取消方法进入。

`scene_instance_prefab.cpp` 实现本场景 Prefab 实例候选的准备、发布、版本替换、移除及绑定查询。
Scene 服务完成入口校验后转交实例，不直接修改这些事务的内部容器；Prefab 源资源编辑仍属于
资产流程。跨场景资产替换继续由 Scene 服务协调，与单场景实例事务的职责不同。

应用级资源管理器位于 `src/engine/resource/asset_manager.hpp`，由 Application 拥有，
管理通用 CPU 请求并唯一持有资源索引；Prefab 与 Render Loader 共享该索引。Render Loader 由 Application
独立拥有，上传通道由应用加载流程装配。资源描述解析同在 resource，不依赖运行时实例化。
管理器不进入安装 SDK，也不拥有编辑状态；缓存实现尚未全部统一到泛型请求。

## Editor 的文档、命令与视口

`src/editor/documents/scene_document.*` 是单个场景的编辑文档，绑定运行时场景，
维护 URI、选择、脏状态和节点查询快照；close 卸载该实例，Application 必须比它长寿。
它不拥有 World，也不负责运行时场景切换。Runtime 的 scene_service 继续管理活动与退役域。

已有编辑操作与历史实现位于 `commands/`，相机、网格与 Gizmo 位于 `viewport/`，
层级、属性、资源、控制台与运行时面板位于 `panels/`。根目录暂保留 UI 装配、主题、
资产监控及运行时通信模型；Host 中的 Editor 装配尚未迁入本层。
`documents/document_editing` 由宿主持有，拥有单个文档、撤销历史及资产/Prefab 编辑控制器。
关闭时拒绝未完成编辑；取消并排空后，先清理历史再关闭文档。它不拥有 Application、
执行器或场景加载事务，也不承担保存提示和多文档管理。
新建场景先准备候选，成功后清理旧历史并替换文档；场景加载事务接收这个所有者，
在途编辑未结束时拒绝新请求并暂停场景域推进。

场景加载事务 `documents/editor_scene_loader`、资产替换事务
`commands/editor_asset_edit`、Prefab 事务 `commands/editor_prefab_edit` 编译在编辑器模块内，
不依赖 Host、进程或 IPC。它们借用 Application、文档及历史；资产/Prefab 编辑的取消与
退出清理由 document_editing 汇总，宿主驱动执行器直至清理结束。场景加载切换独立维护，
流程测试直接链接编辑器模块。窗口、进程及 IPC 仍归 Host。

## 内部命名空间

业务类型按架构层统一使用 `gneiss::engine`、`gneiss::runtime`、`gneiss::editor`、
`gneiss::tooling`、`gneiss::host`。例如资源管理器为 `gneiss::engine::asset_manager`，
World 为 `gneiss::engine`，应用对象为 `gneiss::runtime::application`。
Core、Resource、Render 等目录仍隔离职责和依赖，不再对应额外的命名空间。
单文件辅助代码使用匿名命名空间，头文件实现细节允许 `detail`，不保留旧名兼容别名。
公共值类型仍属于 `gneiss`，公共 C 接口不变。
决策见 [ADR-079](../decisions/ADR-079-flat-namespaces.md)。

## 宿主独立模块

`src/host/common` 保存项目描述和宿主域协议；`src/host/editor` 装配编辑器、项目工具、
子进程和 IPC；`src/host/runtime` 装配运行进程、游戏模块和 IPC；`src/host/assetc` 保存
资产命令行适配。Apps 只转发至宿主入口。Runtime 宿主使用 gneiss::host，
与 gneiss::runtime 的运行时业务对象区分。

Host 可依赖 Engine、Runtime、Editor；后三者不得反向包含 Host。宿主测试暂保留在
各 host 子模块的 tests 目录。Editor Application 中混合的编辑业务/UI 装配尚未全部拆出，
本次宿主目录迁移不表示全部对象职责收敛完成。

## 未保存文档确认

`documents/document_actions` 保存单个待确认的新建、打开或退出动作，取代宿主的 pending 枚举。
它不复制文档脏状态，不拥有文档，不执行文件或进程操作。保存失败/尚未完成时保留请求，
保存成功或放弃时取走一次动作，取消时清空请求。
`panels/document_action_panel` 绘制未保存确认弹窗，通过本次绘制期间的同步回调请求
保存与后续动作；Host 连接文件对话框、场景加载和退出。窗口关闭统一通过 document_editing
取消在途资产、Prefab 与历史操作，排空职责不变。

## 内部资源请求

`engine/resource/asset_request.hpp` 提供移动所有的 `asset_request<T>`；结果为
`asset_ref<const T>`，取消只放弃当前观察者，不取消独立订阅。成功领取一次，失败不改变输出。
`asset_manager` 移植 Granit 示例的共享请求与类型 Loader 模型，固定持有本实例的 VFS 来源；
任务经宿主执行器运行，结果由 `advance_resources()` 发布，同步成功使较早异步失去缓存写入权。
`runtime/loading/action_map_application` 领取结果、复制映射并应用 InputService。
输入实时服务不访问文件或管理请求；公共 ABI 与安装头不暴露这些内部对象。

## CPU 类型与缓存

- `resource/types/material_source.hpp`：纯材质来源描述。
- `resource/types/material_document.hpp`：纯 CPU 材质描述，加载摘要归准备任务；Mesh 直接共享 `mesh_data`。
- `resource/loaders/material_document_decoder.hpp`：消费现有字节的解析函数。
- `resource/loaders/asset_loader.hpp`：显式类型注册与纯 CPU 解码契约，不要求字段反射。
- `resource/vfs/asset_source.*`：有界分步读取、异步打开/关闭与明确的同步回退。

准备结果直接拥有材质参数、纹理 URI 和网格数组。上传准备把网格数组移动到只读共享对象，
由渲染资源和仍在使用它的帧保活；最后一个引用释放时销毁。
不再提供按来源、URI 和内容摘要去重的独立 CPU 文档缓存，见
[ADR-076](../decisions/ADR-076-remove-cpu-document-cache.md)。

统一强引用位于 `resource/asset_ref.hpp`。Prefab 引用直接访问描述并保活缓存拥有者；
渲染资产使用同一种缓存条目引用，专用 Mesh/Material/Texture lease 类已删除。
身份、回收、重载和线程规则见[资产引用契约](../reference/assets.md#缓存与生命周期)。
