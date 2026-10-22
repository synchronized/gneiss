<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 0.51：模块职责、文件布局与对象所有权收敛

## 状态与目标

2026-10-22：下文 CAORS 目录与类型表示既有阶段安排，后续替换以
[资产管理替换计划](PLAN-051-asset-manager-replacement.md)为准，依赖替换已完成。
本页的模块归属和所有权边界继续有效，不以替换依赖为由改变公共 ABI。

2026-10-21 用户确认，实施中。本文是本轮目标布局与迁移顺序的权威来源，
不是当前源码目录说明。决策见 [ADR-069](../decisions/ADR-069-resource-runtime-boundaries.md)。
沿用 0.51 分支，不预先规划 0.52。既有行为与验收仍以
[DEV-051](DEV-051-architecture-boundaries.md) 为准。

目标是从模块接口看懂职责、从对象看懂所有权，减少一次加载或编辑需要追踪的协议。
不以新增类数或移动文件数衡量完成度；每批必须同时列出被删除的旧入口。

## 非目标

- 不开发插件 API，不新增公共 Editor API，不改变 C ABI；公共对象 SDK 按 [ADR-070](../decisions/ADR-070-remove-public-object-sdk.md) 删除。
- 不引入全局 locator，不创建第二套 CAORS 缓存或请求状态机。
- 不在本轮顺带实现 LOD、大场景性能优化或浏览器完整 Editor。
- 不把所有场景强制绑定为独立 World；继续支持一个 World 多个 Scene。

## 目标文件布局

以下是职责布局；仅在确有实现时建立文件，不生成空目录与占位类。

```text
apps/
  editor/main.cpp                    可执行程序入口
  runtime/main.cpp
  project/main.cpp
include/gneiss/                      公共 C ABI 与共享 C++ 值类型，不提供对象 SDK
src/
  engine/
    core/                           日志、数学、任务、诊断、反射基础
    platform/                       io/libuv、窗口、进程、IPC 平台适配
    resource/
      asset_manager.hpp/.cpp        CPU 资源请求、缓存、引用的领域入口
      asset_id.hpp                  稳定身份，与运行时 RID 区分
      asset_ref.hpp                 原生资源引用契约，薄适配
      asset_request.hpp             原生请求契约，按真实使用需要收敛
      types/                        mesh/material/texture/scene/prefab/input_map 描述
      loaders/                      解码、资源依赖声明
      cache/                        现有 CPU 缓存实现，不新增平行缓存
      vfs/                          路径、挂载、容器访问
      backend/caors/                CAORS 注册、请求和缓存实现
    function/
      input/input_service.*         输入映射使用与实时状态
      scene/                        scene、节点、场景实例化
      world/                        ECS、组件、系统及场景归属
      render/                       提交、渲染场景、GPU 资源与 RID
        backend/granit/
    api/                            C ABI 转发与类型转换
  runtime/
    application.*                   服务组装、运行循环与退出顺序
    configuration/                  项目与运行配置
    loading/                        请求完成后的实例化与场景切换流程
    game/                           游戏运行上下文
    api/                            依赖 Application 的 C ABI 适配
  editor/
    editor_application.*
    documents/scene_document.*      文档、选择、撤销与保存状态
    commands/                       编辑操作及撤销重做
    viewport/                      独立编辑器相机、拾取与 Gizmo
    panels/
  host/
    common/                         项目描述与两端宿主协议
    editor/                         Editor 启动、项目工具与子进程/IPC 适配
    runtime/                        Runtime 启动、游戏模块与 IPC 适配
  tooling/                          离线导入、构建与打包
tests/                              对应模块行为与依赖边界验收
vendor/                             第三方源码，继续遵守已有缓存策略
```

资源描述不得包含 GPU 后端对象；资源模块不依赖 function、runtime 或 editor。
各 function 模块显式声明依赖，禁止用聚合头或 locator 隐藏反向依赖。
runtime 可以组装 resource 与 function，editor 可以使用 runtime。
公共头目录无需逐文件镜像 src；内部保留原生 C++，C ABI 只在边界使用。

## 目标对象布局

以下拥有关系为目标示意；现有 session 与服务需逐步合并，不能另建同义运行状态。

```text
editor_application
  application
    task_scheduler
    io_service
    asset_manager
    input_service
    render_service
      render_resource_service
    world
      ECS 与 systems
      scene[]
        scene_node[] → 实体 ID
    scene_loading
  scene_document[] → 场景标识（非拥有运行时场景树）
    selection / undo_history / dirty / 保存路径
  editor_viewport
    editor_camera / gizmo / picking
```

实际类型名称允许复用现有 task_executor、world_service 等；不为图中新名称新增包装。
一个运行环境可拥有多个场景，独立预览或运行环境可拥有另一个 World。
Scene 负责节点层级和实体归属，World 负责实体数据和系统运行。

| 对象 | 拥有和负责 | 不承担 |
| --- | --- | --- |
| asset_manager | 请求去重、缓存、加载状态、资源引用 | 编辑命令、场景修改回调、GPU 提交 |
| scene_asset / prefab_asset | 可保存、可重复实例化的描述 | 活动实体和编辑选择 |
| scene_instantiator | 描述到节点与实体的创建过程 | 独立缓存、编辑历史 |
| scene_document | 编辑状态、撤销重做、保存 | 另一份运行时场景树 |
| render_resource_service | GPU 创建、引用及延迟销毁 | 决定编辑操作成败 |

资源依赖：scene/prefab 描述引用 Mesh 与 Material，Material 引用 Texture。
使用者通过资源强引用维持有效期，缓存按策略保留无人使用资源。节点只关联实体 ID，
ECS 组件保存数据或 RID，不持有 CAORS 类型；内部资源请求与消费者允许使用 CAORS 强引用，公共 ABI 仍隔离第三方类型。
缓存驱逐不能使仍被使用的资源失效；GPU 在途使用由 Render 处理，不依靠 CPU 引用单独保证。

退出顺序：停止新请求与编辑 → 取消并排空在途应用工作 → 释放文档命令及场景资源引用 →
排空渲染使用并释放 GPU 资源 → 停止资源工作并销毁 manager → 最后停止借用的 I/O 与执行器。
具体服务拆分阶段需验证此依赖顺序，不能按成员声明顺序猜测安全性。

## 业务调用路径

- 输入：Application 读取既有 input_map URI → 请求资源 → 成功后 InputService 使用。
  InputService 不读取路径、不查找资产管理器。
- Prefab：编辑命令请求资源 → 完成后验证文档与操作仍有效 → 实例化 → 记录撤销。
- 普通变换：编辑命令修改运行时数据 → 系统更新渲染数据，不经过资源加载。
- 重载：准备资源版本 → 应用层更新使用者 → 成功才提交默认版本；失败保留旧引用。

## 实施批次与删除清单

| 批次 | 工作及删除内容 | 验收 |
| --- | --- | --- |
| A | 移除 asset_manager 执行场景修改回调的 accept；应用层复用既有 prepared_adoption | 准备失败不执行修改，应用失败不切换版本，成功只提交一次 |
| B | 清点 CPU 资源类型、加载器、缓存；将资源描述与 GPU RID 分离 | CPU 资源不依赖 Render/Scene 服务；列出每种资源唯一缓存及遗留例外 |
| C | 收敛 CAORS 缓存与请求，逐类型删除被替代路径 | 同步/异步身份一致、取消、来源变化、失败、无线程与 Web 核心 |
| D | 将 GPU 上传通道与渲染配置移出 asset_manager，由现有 Render/Application 承接 | Manager 不依赖 GPU 上传类型，GPU 在途寿命与回滚测试通过 |
| E | 合并 engine/asset 与 function/asset 到 resource；Application 组装迁到 runtime | 更新 CMake、私有头清单、依赖负向检查与安装消费 |
| F | 收敛编辑状态与场景实例，删除重复 session/辅助流程 | Prefab 异步放置、撤销重做、切换取消、关闭及重载回归 |

每批先调整职责再移动文件，不要求同一提交完成整个目录树。既有版本发布与候选隔离规则
在替代机制通过测试前保留；不得为了简化接口取消失败原子性。
批次 A 已通过本地验证，见[验收记录](../records/2026-10-21-resource-adoption-boundary.md)；
批次 B 已完成资源盘点与输入资源职责迁移，见[记录](../records/2026-10-21-cpu-resource-boundaries.md)，
Prefab/Scene 描述与作者属性值已迁入资产层，Registry 覆盖编辑留在功能层，见
[描述分离记录](../records/2026-10-21-scene-resource-descriptions.md)。GPU 租约与其余职责仍待收敛，
批次 B 尚未整体完成。批次 D 已先行移除管理器的上传通道工厂、设备配置和完成回执依赖，
见[渲染通道分离记录](../records/2026-10-21-render-channel-boundary.md)。渲染缓存和 Loader
所有权也已迁至 Application，资产管理器不再依赖 Render，见[所有权验收](../records/2026-10-21-render-cache-ownership.md)。
批次 D 的本地实现已完成，完整平台验收与 CPU 缓存合并仍未完成。详细 CAORS 平台限制见
[资源路径收敛计划](PLAN-051-caors-convergence.md)。

## 总体验收与风险

- 新布局必须有可执行依赖检查，防止 resource 反向包含 function/runtime/editor。
- 从一次输入加载、Prefab 放置与场景切换能找到唯一的流程负责对象。
- 共享/静态库、C/C++ consumer、原生线程/协作与 Web 核心需按受影响范围验证。
- 保持原有 URI、错误、取消、失败保留原状态和关闭寿命契约；不新增临时功能开关。
- 同步缓存发布代次、材质/Mesh 缓存合并、GPU 与 CPU 租约拆分仍需实现验证。
  遇到必须改变外部行为或资源寿命契约的取舍时，先列出方案交用户确认。

## 目录落地状态（2026-10-21）

已将 engine/asset、function/asset 与 Prefab 资源 Loader 合并为 `src/engine/resource/`，
旧目录与旧头路径已删除，CMake、内部头清单、测试和边界规则使用新路径。
资源内部已按 types/loaders/vfs/cache/backend 分类，Application、加载流程、游戏上下文
与应用相关 C 适配已迁入 src/runtime。批次 E 的源码目录迁移已落地；跨平台及安装消费
验收仍待完成。现有对象和缓存实现保持不变，后续继续收敛重复状态。
当前结构以[源码布局](../concepts/repository-layout.md)为准。

## 场景对象收敛状态

场景域已封装 World 与实例服务所有权，移除重复 World RID 以及 VFS、Render、Prefab
加载器引用；使用者显式访问真正的资源所有者。初始化成功后才发布 World 与实例服务，
Session 初始化成功后才发布活动域。

暂保留三种对象：session 拥有活动/退役域，load_service 拥有在途候选，scene_state
拥有一个 World 及其 Scene 实例。它们不是三份相同场景状态，不能为减少类数量合并
而改变取消、旧状态保留和延迟清理。Session 中的资源重载编排仍待独立审查，批次 F
尚未整体完成。

## Editor 目录与文档对象迁移

已将 editor_session 改为 documents/scene_document，调用方和 CMake 使用新名称，
不保留别名。已有命令、视口与面板分别归入 commands/viewport/panels；没有新增对象状态。
文档维持现有 close 卸载场景契约；文档、历史和在途资产/Prefab 编辑已归入
document_editing。后续继续审查场景切换与模态 UI 编排，再迁移 Editor Application；不直接移动进程/IPC 代码
使 src/editor 反向依赖 Apps。验收见
[Editor 布局记录](../records/2026-10-21-editor-document-layout.md)。

场景加载、资产替换与 Prefab 异步控制器已从 apps/editor 移至 documents/commands，
从宿主库移入编辑器库，三项流程测试直接链接编辑器库；没有新增协调器或重复状态。
宿主仍负责模态 UI、请求互斥与执行器驱动；取消及关闭顺序已由 document_editing 汇总。
见[控制器迁移验收](../records/2026-10-21-editor-async-controller-boundary.md)。

## 命名空间统一（2026-10-21）

按已确认目录职责统一，不再以 xxx_internal 作为模块名称。目标：Engine 的 core/platform/
resource 及 function 下 world/scene/render/input 分别使用 gneiss::engine 对应子命名空间；
function 不进入命名空间。Runtime 使用 gneiss::runtime，Editor 使用 gneiss::editor。
模块内部辅助实现采用 detail 或匿名命名空间。私有性仍由安装和 target 可见性控制。

分批顺序：Runtime 的 Application/加载/游戏上下文；Engine resource；各 function 领域；
core/platform 及剩余边界辅助。每批移除旧声明与引用，不使用兼容别名。
World 类须随领域迁入 gneiss::engine::world::world，避免与领域命名空间冲突。
不修改公共 C 符号及共享值类型，不增加运行时状态；内部 C++ 消费者需同源码重编译。

Runtime 首批命名迁移已验证，见[验收记录](../records/2026-10-21-runtime-namespace.md)。Engine 各领域仍待迁移。

资源命名首批范围：asset_internal、asset_manager、action_map_request 及其 CAORS 工厂
统一为 gneiss::engine::resource。Scene/Prefab 作者描述保留为待迁移项，不能将功能层的
scene_internal 整体替换到资源层。边界检查拒绝恢复旧资产命名，行为与所有权不变。

资源首批验收见[实施记录](../records/2026-10-21-resource-namespace.md)。

Scene/Prefab 作者描述、属性覆盖值、解析/序列化与 Prefab Loader 已迁入资源命名空间，
功能层调用方显式引用资源类型。旧 scene_internal 仅保留功能层行为；没有兼容别名。
资源目录不再出现 scene_internal，边界正反例检查防止回流。验收见
[描述命名空间迁移](../records/2026-10-21-scene-resource-namespace.md)。

World/Scene/Render/Input 领域与调用方已迁入 gneiss::engine 对应子空间，
World 类使用 gneiss::engine::world::world；不保留旧名称，资源描述保持 resource 归属。
共享输入值当前仍位于 core/input 文件路径，随 Input 领域使用 engine::input；
后续 core 统一时再审查共享值归属，不把此命名声明为目录已完全逐一对应。
验收见[功能层命名迁移](../records/2026-10-21-function-namespace.md)。

Core/Platform 命名首批：基础工具为 engine::core，任务/诊断/日志/反射/数学/输入值
使用其领域子空间；窗口、I/O 和 CAORS 适配为 engine::platform 及其 io/caors 子空间。
C 值转换集中在 engine::api，应用日志回调桥归 runtime::api；移除旧别名。
第三方 CAORS 使用 ::caors，避免适配子空间遮蔽。进程、IPC 基础设施与动态库对象也已迁入 engine::platform，宿主域协议保持原归属。

Core/平台适配首批验收见[记录](../records/2026-10-21-core-platform-namespace.md)。

文档编辑所有权收敛验收见[记录](../records/2026-10-21-document-editing-ownership.md)。
当前未迁移场景切换 UI 和宿主整体装配，不将本批视为 Editor 迁移完成。

## 本轮剩余迁移与宿主边界

- 文档新建和加载切换入口已接收 document_editing，在途编辑阻止场景域切换。
  未保存确认状态和弹窗已迁入 Editor；其余场景切换 UI、操作互斥和 Editor Application 装配仍待迁移。
- 平台进程、IPC 基础设施和动态库命名已统一，验收见
  [记录](../records/2026-10-21-platform-object-namespace.md)。
- CPU 缓存共享发布代次、材质/Mesh 缓存与 CPU/GPU 引用拆分仍未完成，按批次 B/C 推进。
- Runtime 场景重载编排及完整平台验收仍待完成。

用户已确认方案 B：Apps 只留入口，宿主适配迁入 src/host 独立模块；Host 使用
Editor/Runtime/Platform，业务模块不得反向包含 Host。先迁移宿主代码和构建，再拆分
宿主中的编辑业务/UI 装配；不把仅移动文件视为整个架构迁移完成。

Host 目录和四个薄入口已落地，见[宿主布局验收](../records/2026-10-21-host-layout.md)。
迁移保留原 target 和协议，不新增动态库；业务/UI 拆分与资源批次仍按上述剩余项继续。

未保存确认流程拆分验收见[记录](../records/2026-10-21-editor-document-actions.md)。

## 通用资源请求（2026-10-21）

已用薄的 asset_request<T> 替换输入专用虚接口与工厂，复用 CAORS 状态与强引用。
Runtime 保留输入采用责任；同步缓存发布及其他资源路径的收敛仍待完成。
验证见[迁移记录](../records/2026-10-21-generic-asset-request.md)。

## 材质资源领域边界（本批完成）

已将 material_source、material_document 与 mesh_document 放入 resource/types，
材质纯字节解码归 loaders，已验证版本索引和具体只读租约归 cache。
backend/caors 仅保留加载器适配，不再充当资源类型与版本策略入口。
删除仅用于隐藏具体租约的空虚基类，准备结果与渲染保活字段使用明确的租约类型。

保持整批来源复验、确切版本选择、旧租约存活及 GPU 资源生命周期不变。
验收覆盖材质版本复用、失败不发布、跨批次保活、纹理准备/采用与模块边界。
本批不合并同步缓存发布、不增加 I/O 或执行器，不宣称 CPU/GPU 全部拆分完成。

本批验证见[材质领域迁移记录](../records/2026-10-21-material-resource-boundaries.md)。

## CPU 文档缓存对象收敛（本批完成）

删除只有一个实现的 cpu_document_cache/cpu_document_batch 虚接口、隐藏派生类和工厂。
直接构造具体类；保留共享版本索引与每批独立候选，禁止复制缓存和批次所有者。
不修改来源复验、材质/Mesh 去重、准备线程与完成线程交接，以及失败/取消协议。
验收覆盖批次跨缓存外壳生命周期、跨批次共享、失败不发布和纹理服务集成。

本批实现与验证见[缓存对象迁移记录](../records/2026-10-21-cpu-cache-objects.md)。
