<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-329：架构审计记录

2026-10-15，首轮静态审计。对应 [DEV-051](../plans/DEV-051-architecture-boundaries.md)。
本记录不是完整审计验收，尚未修改源码或重新采集性能数据。

## 已确认的加载链路

| 位置 | 当前职责与所有权 |
| --- | --- |
| `apps/editor/editor_scene_loader.cpp` | 接收打开意图，推进进度和取消，通过内部 Application 接口协调编辑器状态 |
| `src/engine/function/application/application_host.cpp` | 解析 Application 句柄、验证状态，转交加载请求；使用整数句柄不等于经过外部 C ABI |
| `src/engine/function/application/scene_load_service.hpp` | 候选事务、进度、取消及清理状态；借用执行器和渲染服务，不自行创建线程 |
| `src/engine/function/application/application_scene_state.hpp` | 拥有场景域的缓存、资产加载器、预制体加载器、World 和场景实例服务；借用共享渲染资源服务 |
| `src/engine/function/application/application_scene_state.cpp` | 析构先释放场景实例服务，再销毁 World；随后成员按 C++ 析构规则释放 |
| `src/engine/function/application/application_state.cpp` | 接收已准备候选，交换活动域、待退役域及纹理服务；持有跨模块关闭顺序 |

`activate_scene` 检查待退役域和纹理任务状态后调用 `take_candidate`，将旧活动域移入
`retired_scene_`，再替换活动域和纹理服务。候选准备与活动场景切换分属两个位置。
`application_scene_state` 的接口契约要求域由 owner thread 管理；已提交帧的快照拥有独立持有期。
后续仍需核对快照实现和迟到回执的全部路径，不能仅凭声明判定生命周期审计完成。

关闭入口先销毁场景加载服务、纹理服务及场景域，随后关闭渲染服务，最后才释放平台和资产 I/O
执行器。移动职责时必须保留这条资源依赖顺序，不能按目录名称独立析构各模块。

## 已有边界设施

`src/CMakeLists.txt` 集中组装 Engine 源码；`src/engine/core/CMakeLists.txt` 已有内部值类型
目标和 tasks 子目录。不是所有模块都没有构建边界，也不需要为每个模块新增动态库。

`cmake/check_core_boundary.cmake` 已检查 Engine 到 Apps/Editor 的反向依赖，以及 core、platform、
asset、render 等层的若干越界包含和 C API 使用。`cmake/test_core_boundary.cmake` 提供负向夹具。
这些检查可继续扩展；尚不能据此认定所有私有头已实现 target 级隔离。

## 待讨论的方案

后续状态：用户已确认方案 A，正式决策见
[ADR-063](../decisions/ADR-063-scene-lifecycle-coordination.md)。下文保留首次审计时的备选分析。

加载服务直接包含 `application_scene_state.hpp`，候选域又组合资产、场景和渲染能力。
把现有加载服务原样下移到 scene 会把上层装配依赖一起带入。

- 方案 A（建议）：在 Engine 的 application 模块内明确独立的场景生命周期协调组件，拥有加载事务、
  活动域与退役域；Application 负责装配、主循环调用和整体启停。scene 保留结构与实例能力。
  第一批不增加新的顶层目录，不改变公开 SDK。组件最终命名和接口在确认职责后确定。
- 方案 B：把完整加载和运行域都归 scene。打开场景的入口更集中，但 scene 会负责资产准备、
  渲染资源协调和宿主切换策略，必须重新定义 scene 的层级及允许依赖。

建议 0.51 先交付完整场景生命周期链路、相关构建边界与回归，不同时重排所有子系统。
其余架构工作保留待办，不指定 0.52；此范围仍待用户确认，不是已实施决策。

## 验证与未完成项

确认方案 A 后继续核对：`render_frame_packet.hpp/.cpp` 的帧资源快照通过
`share_mesh`、`share_material`、`share_texture` 获取共享持有，未直接借用场景域内对象地址。
这支持场景域与提交帧分离的方向，但尚需完成后端帧完成与 GPU 资源释放路径核对。
`attach_task_executor` 还装配 libuv I/O 与 Granit 上传回调；这些后端装配应留在 Application，
只将加载服务创建和所属场景状态移入 session，避免新组件重新变成平台入口。

本轮内部边界检查及负向夹具通过。首次以相对源码根运行检查被空源码保护拒绝，改为绝对源码根
后通过；未绕过检查。仅修改文档，未运行完整构建或 Sponza。

0.50 基线和已接受限制仍以 [0.50.0 归档](../versions/0.50.0.md) 为准。
M-329 尚缺：全模块依赖与接口清单、快照/回执所有权核对、逐文件迁移表，以及实施前固定的
同设备性能对照方法、重复次数和回归判定。完成这些项目之前不标记审计完成。
