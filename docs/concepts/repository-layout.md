<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 源码目录与模块所有权

## 目的

本文描述当前实际源码布局，不把已批准的迁移目标当成已实现。0.45 的精简目标目录与宿主边界见
[ADR-054](../decisions/ADR-054-source-layout-and-host-boundaries.md)，迁移映射与验收见
[DEV-045](../plans/DEV-045-cpp-boundaries.md)。Core、Platform、Asset 与 Function 已迁入 `src/engine/`，
C ABI 适配已迁入 `src/engine/api/`，`src/editor/` 已迁入 UI、资产服务、独立编辑模型与场景会话，宿主控制及面板编排仍待拆分；
`apps/` 保持位于仓库根目录，编辑器实现与入口的拆分尚未完成。

总体分层以[总体架构](architecture.md)为准，代码与文档规范分别以
[C/C++ 代码风格](../guides/coding-style.md)和[项目文档规范](../../DOCUMENTATION_GUIDE.md)为准。

## 当前顶层目录

| 目录 | 所有权与职责 |
| --- | --- |
| `include/gneiss/` | 稳定的 C11 公共接口及其轻量 C++20 包装 |
| `src/` | 内部运行时实现、第三方适配及离线处理 |
| `apps/` | Editor、Runtime、assetc 程序入口及宿主共用代码 |
| `scripts/` | 仓库维护与性能测量脚本 |
| `tests/` | 公共接口、内部行为、生命周期与集成验证 |
| `examples/` | 使用公共接口构建的独立最小示例 |
| `docs/` | Guide、Reference、Concept、Plan、ADR 和执行记录 |
| `3rd/` | 锁定版本并与自有代码隔离的第三方依赖 |
| `cmake/` | 项目构建策略和可复用的 CMake 模块 |

当前 `gneiss_engine` 的四层实现已集中在 `src/engine/`，C ABI 适配位于 `src/engine/api/`。
Runtime 宿主与 Editor 入口分别位于 `apps/runtime/` 和 `apps/editor/`；两者可以依赖 Engine Library，
Engine Library 不得反向依赖它们。Editor 的 ImGui Context、字体、主题、DockSpace 和通用控件由
`src/editor/` 中不安装、不导出的 `gneiss_editor_ui` 内部静态库统一管理，面板选择状态仍由 Editor 应用层持有。
控制台面板由 `console_panel.hpp/.cpp` 绘制，借用日志模型及进程展示值；清空请求通过宿主同步回调执行。
作者属性 Inspector 由 `author_property_panel.hpp/.cpp` 绘制，属性写入与撤销事务归
`author_property_edit.hpp/.cpp` 所在的会话目标；Apps 只提供同步连接，不把应用状态交给 UI 实现。
Runtime 层级树和属性面板由 `runtime_panels.hpp/.cpp` 绘制，只读取 Editor 模型；
Apps 在每次绘制时提供同步属性提交、回写作者场景与结果报告回调，协议值只在宿主边界适配。

编辑器资产浏览、监听、导入控制、重导入队列与后台服务也位于 `src/editor/`，
由 `gneiss_editor_assets` 内部静态库组织，依赖 Engine、Tooling、任务调度和平台 IO。
该目标不依赖 Apps 或编辑会话；入口仍在 Apps 装配服务并轮询结果。
资产面板位于 UI 目标，仅借用资产模型和单帧显示值；Apps 适配协议与进程状态。面板返回拥有
选择身份的请求，宿主在绘制结束后执行，避免绘制期间的服务变化使借用输入失效。

`gneiss_editor_model` 位于 `src/editor/`，管理相机、网格、旋转/Gizmo 数学、命令历史、属性检查
及创作事务、控制台模型，不依赖 Apps 工程、IPC 或会话。场景会话与 Gizmo 拖拽由同目录的
`gneiss_editor_session` 管理，只依赖该模型库与 Engine。`apps/editor/` 的 `gneiss_editor_host`
负责工程、子进程、IPC 与运行时同步装配。

`apps/common/` 保留两个宿主私有目标：`gneiss_app_project` 管理构建/启动工程描述及启动日志协议，
`gneiss_app_ipc_protocol` 管理 Editor/Runtime 进程通信协议。它们不安装、不导出为 Engine SDK；
Runtime 直接使用这些共用契约，不链接 Editor。通用 IPC 传输和子进程机制由 Engine Platform 提供，
工程格式、消息领域与启动约定继续归 Apps。Apps 私有 C++ 头统一使用 `.hpp`，不属于公共 C ABI。

`asset_scene_commands.hpp/.cpp` 属于 Editor 会话目标，统一添加 Mesh/Prefab 与替换资源的撤销事务；
它只接收作者身份和资产 URI，不依赖资产面板、导入服务或宿主协议。

`src/editor/runtime_author_apply.hpp` 接收 UUID 与局部变换视图，负责变换回写和撤销命令。
Apps 从 IPC 节点提取输入；Editor 命令复制身份字符串，不保留协议对象或借用字符串。
控制台接收拥有字符串的 `console_event`；Apps 的日志适配负责从已解析协议记录移动字段，
控制台不保存协议版本。属性编辑状态也已归入独立模型，使用 Editor 自有请求、结果与类型化属性值；Apps 适配 IPC，
模型继续唯一管理 pending、乱序响应、超时和断连。场景镜像同样位于独立模型库，
使用 `runtime_scene_data.hpp` 的节点批次，唯一管理分块、顺序与图校验。宿主只适配解码结果，
镜像与属性模型共用 `runtime_object_id.hpp`；不向 Runtime 传播 Editor 库依赖。
运行前的场景保存与启动条件判断由 `src/editor/runtime_launch.hpp` 提供；进程启动仍归 Apps。

离线导入库的构建定义与 C++ 实现集中于 `src/tooling/`，内部头使用 `.hpp`。
assetc CLI 位于 `apps/assetc/`，工具测试由 `tests/tooling/` 组织，性能脚本位于 `scripts/performance/`。

公共头目录按能力组织，但 `.h` 与 `.hpp` 始终成对维护。内部源码目录按拥有运行时状态的模块组织，
不机械复制公共头目录。

## 运行时模块

以下为当前实现路径；按各模块实际迁移结果更新。

| 模块 | 目录 | 职责 | 状态 |
| --- | --- | --- | --- |
| Core | `src/engine/core/` | 结果、RID、Service 注册等无领域倾向的基础设施 | 已存在 |
| World | `src/engine/function/world/` | World、Entity、System 与 EnTT 适配 | 已存在 |
| Scene | `src/engine/function/scene/` | Scene Tree、节点映射与层级 Transform | 已存在 |
| Application | `src/engine/function/application/` | 顶层生命周期、初始化回滚和主循环编排 | 已存在 |
| Platform | `src/engine/platform/` | 窗口、IO、IPC、子进程和动态库等平台能力的隔离 | 已存在 |
| Asset | `src/engine/asset/` | VFS、格式、CPU 解码与缓存 | 已迁移 |
| Render | `src/engine/function/render/` | 渲染输入快照、资源 RID、帧包与执行器 | 已存在 |
| Input | `src/engine/function/input/` | 输入状态与动作映射 | 已迁移 |
| Game | `src/engine/function/game/` | Game Module 上下文与宿主桥接 | 已迁移 |
| Reflection | `src/engine/core/reflection/` | 唯一类型注册表与属性访问 | 已存在 |
| Log | `src/engine/core/log/` | 通用文本校验与异步事件投递；Application 独立适配公共回调 | 已迁移 |
| C ABI | `src/engine/api/` | 公开描述适配、异常隔离及内部入口委托 | 已存在 |
| Tooling | `src/tooling/` | 离线资产处理实现 | 已存在 |
| Granit 后端 | `src/engine/function/render/backend/granit/` | Granit 类型、调用和错误转换的隔离 | 已存在 |

Platform 的 Granit Window 适配位于 `src/engine/platform/granit/`；`src/engine/function/render/backend/granit/` 只负责
渲染后端，两者不共享原生对象所有权。窗口配置与原生窗口视图归 `gneiss::platform`；
Application 的 C 入口将版本化描述转换为内部 `application_configuration`，Platform 只接收
`window_configuration`。用户回调仍是实际 ABI 边界，不将回调上下文复制成另一套状态。渲染快照的值类型位于 `src/engine/function/render/render_snapshot.hpp`，
World 的提取函数位于 `src/engine/function/world/render_snapshot.hpp`，Render 不反向包含 World/Scene/Application。
`src/engine/asset/png_decoder.hpp` 提供 CPU 解码，Cook 与运行时加载共用；解码不依赖渲染服务。

Scene 的节点创建使用内部借用参数，普通节点与 Prefab 查询返回内部视图。公共查询结构的版本写回
归 `src/engine/api/scene_query_conversion.hpp`；核心查询不依据 ABI 尺寸分支。字符串寿命仍由 Scene 管理。

纹理加载与上传事务由 `src/engine/function/render/texture_load_service.hpp` 组织，属于 Render 功能层；
其依赖 Asset 的 VFS，但 Asset 不反向包含 Render。

Render 的同步资源创建接收内部 `mesh_view`、`texture_view` 与 `material_resource`，借用数组只在
创建调用期间有效。公共描述的尺寸、版本、保留字段与指针/长度转换归 `src/engine/api/`；资源服务
继续负责数值、拓扑、RID 与预算校验，成功后拥有数据副本。资产加载器不生成版本化 C 描述。

`render_asset_loader` 暂仍位于 Render，它连接 Asset 缓存租约与 Render 资源发布，并不只是格式
解码器。CPU 准备和解析实现位于 `src/engine/asset/asset_preparation.hpp/.cpp`，
Render 的准备适配只传入发布资源的预算。三类准备数据均使用 Asset 值类型，材质依赖仅用 URI，
发布边界绑定纹理 RID 并移动纹理数组；上传弱引用仍由 Render 管理。Asset 不包含 Render 实现，
独立准备测试不链接 Engine、缓存或资源服务。其余迁移见 [0.45 实施计划](../plans/DEV-045-cpp-boundaries.md)。

当前测试分布在 tests 根目录及 `tests/core/`、`tests/world/`、`tests/scene/` 等模块目录。
公共头独立编译位于 `tests/headers/`，安装消费者位于 `tests/consumer/`；跨模块工作流还通过
CMake 脚本验证。尚未统一迁移到新的测试层级，不创建空的 integration 目录。

## 演进规则

- 新目录必须对应一个已经开始实施的模块或工具，不添加占位文件。
- 新模块先确认职责、拥有的状态、公开接口和允许依赖；跨层变化使用 ADR 记录。
- 第三方后端放在所属 Service 下面，不建立可被所有模块随意依赖的通用 `backend/`。
- 平台差异集中在 `src/engine/platform/` 或具体后端目录，不散布到 World、Scene 和业务组件。
- 单个实现文件只被一个模块使用时留在该模块内部；只有形成稳定跨模块契约后才提升为公共接口。
- 目录调整应伴随真实代码迁移和验证，不单独进行大规模结构美化。
