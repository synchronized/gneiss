<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 0.51：模块职责、文件布局与对象所有权收敛

## 状态与目标

2026-10-21 用户确认，实施中。本文是本轮目标布局与迁移顺序的权威来源，
不是当前源码目录说明。决策见 [ADR-069](../decisions/ADR-069-resource-runtime-boundaries.md)。
沿用 0.51 分支，不预先规划 0.52。既有行为与验收仍以
[DEV-051](DEV-051-architecture-boundaries.md) 为准。

目标是从模块接口看懂职责、从对象看懂所有权，减少一次加载或编辑需要追踪的协议。
不以新增类数或移动文件数衡量完成度；每批必须同时列出被删除的旧入口。

## 非目标

- 不开发插件 API，不新增公共 Editor API，不改变公共 SDK 命名空间与 ABI。
- 不引入全局 locator，不创建第二套 CAORS 缓存或请求状态机。
- 不在本轮顺带实现 LOD、大场景性能优化或浏览器完整 Editor。
- 不把所有场景强制绑定为独立 World；继续支持一个 World 多个 Scene。

## 目标文件布局

以下是职责布局；仅在确有实现时建立文件，不生成空目录与占位类。

```text
apps/
  editor/main.cpp                    可执行程序入口
  runtime/main.cpp
include/gneiss/                      保留当前公共 C ABI 与 C++ SDK 组织
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
    project_settings.*              项目配置读取与使用
    scene_loading.*                 请求完成后的实例化与场景切换流程
  editor/
    editor_application.*
    documents/scene_document.*      文档、选择、撤销与保存状态
    commands/                       编辑操作及撤销重做
    viewport/                      独立编辑器相机、拾取与 Gizmo
    panels/
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
ECS 组件保存数据或 RID；资源引用由场景绑定集合或对应服务持有，不泄漏 CAORS 类型。
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
见[渲染通道分离记录](../records/2026-10-21-render-channel-boundary.md)；渲染租约所有权仍待拆分，
不代表批次 D 或缓存合并完成。详细 CAORS 平台限制见
[资源路径收敛计划](PLAN-051-caors-convergence.md)。

## 总体验收与风险

- 新布局必须有可执行依赖检查，防止 resource 反向包含 function/runtime/editor。
- 从一次输入加载、Prefab 放置与场景切换能找到唯一的流程负责对象。
- 共享/静态库、C/C++ consumer、原生线程/协作与 Web 核心需按受影响范围验证。
- 保持原有 URI、错误、取消、失败保留原状态和关闭寿命契约；不新增临时功能开关。
- 同步缓存发布代次、材质/Mesh 缓存合并、GPU 与 CPU 租约拆分仍需实现验证。
  遇到必须改变外部行为或资源寿命契约的取舍时，先列出方案交用户确认。
