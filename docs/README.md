<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Gneiss 文档中心

这里是 Gneiss 使用指南、接口参考、架构说明和开发计划的统一入口。根 README 只负责项目介绍与最小入口；具体事实以本页链接的对应文档为准。

## 推荐阅读顺序

### 了解项目

1. 阅读项目根目录的 [README](../README.md)，了解定位与当前状态。
2. 阅读[项目目标与边界](concepts/project-scope.md)，了解 Gneiss 解决的问题和与 Granit 的分工。
3. 阅读[总体架构](concepts/architecture.md)，了解场景树、ECS、服务与后端的关系。
4. 阅读[开发路线图](roadmap.md)，了解阶段、优先级和当前工作重点。

### 参与开发

1. 阅读[项目文档规范](../DOCUMENTATION_GUIDE.md)。
2. 阅读 [C/C++ 代码风格与语言标准](guides/coding-style.md)。
3. 通过 [GitHub Issues](https://github.com/synchronized/gneiss/issues) 确认任务范围。

## 操作指南

- [构建、测试与运行示例](guides/building.md)
- [从 0.9.0 迁移到 0.10.0](guides/migrating-0.9-to-0.10.md)
- [从 0.10.0 迁移到 0.11.0](guides/migrating-0.10-to-0.11.md)
- [C/C++ 代码风格与语言标准](guides/coding-style.md)
- [在 Editor 中导入资产](guides/editor-assets.md)

## API 参考

- [API 稳定级别与兼容策略](reference/compatibility.md)
- [Core 版本与结果接口](reference/core.md)
- [RID 有效性与 Service 生命周期](reference/rid.md)
- [World、Entity 与内部 ECS 边界](reference/world.md)
- [Scene Tree、Entity 映射与 Transform](reference/scene-tree.md)
- [Application 生命周期、时间与主循环](reference/application.md)
- [输入事件与状态快照](reference/input.md)
- [输入动作映射格式 v1](reference/input-map-format.md)
- [诊断回调](reference/diagnostics.md)
- [日志消息契约](reference/logging.md)
- [Type Registry 与反射元数据](reference/reflection.md)
- [Render 资源、ECS 组件与帧提取](reference/render.md)
- [资产 URI、目录挂载与缓存](reference/assets.md)
- [场景文件格式 v4](reference/scene-format.md)
- [场景加载、实例与卸载](reference/scene-instance.md)
- [工程文件格式 v1](reference/project-format.md)
- [Game Module ABI 与生命周期](reference/game-module.md)
- [Mesh Binary、JSON Mesh 与 Material 资产格式](reference/render-asset-formats.md)
- [Editor 资产索引格式 v1](reference/asset-index-format.md)

## 架构与原理

- [项目目标、核心边界与非目标](concepts/project-scope.md)
- [总体架构、模块边界与核心技术](concepts/architecture.md)
- [源码目录、模块所有权与演进规则](concepts/repository-layout.md)

## 路线图与开发计划

- [Gneiss 开发路线图](roadmap.md)
- [VER-001：0.1.0 最小运行时闭环](plans/VER-001-0.1.0-runtime-slice.md)
- [VER-002：0.2.0 资源与场景闭环](plans/VER-002-0.2.0-resource-scene-slice.md)
- [VER-003：0.3.0 交互与诊断闭环](plans/VER-003-0.3.0-interaction-diagnostics-slice.md)
- [VER-004：0.4.0 纹理与材质闭环](plans/VER-004-0.4.0-texture-material-slice.md)
- [VER-005：0.5.0 基础 3D 场景闭环](plans/VER-005-0.5.0-basic-3d-slice.md)
- [VER-006：0.6.0 数据与属性基础](plans/VER-006-0.6.0-data-property-slice.md)
- [VER-007：0.7.0 编辑器基础闭环](plans/VER-007-0.7.0-editor-foundation-slice.md)
- [VER-008：0.8.0 编辑器资产工作流](plans/VER-008-0.8.0-editor-asset-workflow.md)
- [VER-009：0.9.0 场景创作工作流](plans/VER-009-0.9.0-scene-authoring-workflow.md)
- [VER-010：0.10.0 稳定性预览](plans/VER-010-0.10.0-stability-preview.md)
- [VER-011：0.11.0 Runtime 宿主工作流](plans/VER-011-0.11.0-runtime-workflow.md)
- [VER-012：0.12.0 游戏模块与生命周期](plans/VER-012-0.12.0-game-module-lifecycle.md)
- [VER-013：0.13.0 结构化日志与 Editor Console](plans/VER-013-0.13.0-structured-logging-console.md)
- [VER-014：0.14.0 Editor UI 工作区](plans/VER-014-0.14.0-editor-ui-workspace.md)
- [VER-015：0.15.0 Editor–Runtime 双向控制通道](plans/VER-015-0.15.0-editor-runtime-ipc.md)
- [VER-016：0.16.0 Runtime 调试与状态检查](plans/VER-016-0.16.0-runtime-inspection.md)
- [VER-017：0.17.0 Runtime 实时属性编辑](plans/VER-017-0.17.0-runtime-property-editing.md)
- [VER-018：0.18.0 Prefab 与可复用场景基础](plans/VER-018-0.18.0-prefab-foundation.md)
- [VER-019：0.19.0 Prefab 实例属性覆盖](plans/VER-019-0.19.0-prefab-overrides.md)
- [VER-020：0.20.0 Prefab 作者工作流](plans/VER-020-0.20.0-prefab-authoring.md)
- [VER-021：0.21.0 IPC 模块化重构](plans/VER-021-0.21.0-ipc-modularization.md)
- [VER-022：0.22.0 资产热重载](plans/VER-022-0.22.0-asset-hot-reload.md)
- [M-100：Editor Play 工作流实施记录](records/M-100-editor-play-workflow.md)
- [M-101：示例与跨平台验收记录](records/M-101-example-cross-platform-validation.md)
- [M-102：Runtime 检查会话与对象标识实施记录](records/M-102-runtime-inspection-identity.md)
- [M-103：Runtime 场景快照与增量生成实施记录](records/M-103-runtime-scene-snapshot.md)
- [M-104：Editor Runtime 镜像与层级视图实施记录](records/M-104-editor-runtime-scene-mirror.md)
- [M-105：Runtime 只读属性检查实施记录](records/M-105-runtime-property-inspection.md)
- [M-106：Runtime 基础运行统计实施记录](records/M-106-runtime-statistics.md)
- [M-107：Runtime 检查流控与恢复实施记录](records/M-107-runtime-inspection-recovery.md)
- [M-108：Runtime 检查示例与跨平台验收记录](records/M-108-runtime-inspection-validation.md)
- [M-109：Runtime 属性写入协议实施记录](records/M-109-runtime-property-edit-protocol.md)
- [M-110：Runtime 属性写入执行实施记录](records/M-110-runtime-property-execution.md)
- [M-111：Editor Runtime 属性命令状态实施记录](records/M-111-editor-runtime-property-state.md)
- [M-112：Transform 运行态编辑实施记录](records/M-112-runtime-transform-editing.md)
- [M-113：Runtime Transform 作者回写实施记录](records/M-113-runtime-transform-author-apply.md)
- [M-114：Runtime 属性编辑恢复与过载实施记录](records/M-114-runtime-property-recovery.md)
- [M-115：Runtime 属性编辑验收记录](records/M-115-runtime-property-editing-validation.md)
- [TOOL-001：glTF 资产编译器最小闭环](plans/TOOL-001-gltf-asset-compiler.md)
- [TOOL-002：Mesh Binary v1 最小闭环](plans/TOOL-002-mesh-binary-v1.md)
- [TOOL-003：索引渲染最小闭环](plans/TOOL-003-indexed-rendering.md)

## 架构决策

- [架构决策索引](decisions/README.md)
- [ADR-001：Granit 依赖接入边界（已取代）](decisions/ADR-001-granit-dependency.md)
- [ADR-002：ECS、反射与序列化边界](decisions/ADR-002-ecs-reflection-boundary.md)
- [ADR-003：Granit 依赖来源策略](decisions/ADR-003-granit-provider-strategy.md)
- [ADR-004：使用 yyjson 解析内部 JSON 文档](decisions/ADR-004-json-parser.md)
- [ADR-005：资产 URI 与虚拟文件系统边界](decisions/ADR-005-asset-uri.md)
- [ADR-006：场景 Schema v1 持久化契约](decisions/ADR-006-scene-schema-v1.md)
- [ADR-007：首版 Mesh 与 Material 运行时资产格式](decisions/ADR-007-render-asset-formats.md)
- [ADR-008：动作标识与帧状态折叠](decisions/ADR-008-action-identity-and-folding.md)
- [ADR-009：同步诊断回调](decisions/ADR-009-synchronous-diagnostics.md)
- [ADR-010：纹理资源与后端镜像边界](decisions/ADR-010-texture-resource-boundary.md)
- [ADR-011：使用 libspng 解码首版 PNG 纹理](decisions/ADR-011-png-decoder.md)
- [ADR-012：3D 坐标、Camera 与后端变换边界](decisions/ADR-012-3d-camera-coordinate-boundary.md)
- [ADR-013：glTF 离线导入与工具边界](decisions/ADR-013-gltf-import-boundary.md)
- [ADR-014：源资产、作者格式与 Runtime 二进制资产分层](decisions/ADR-014-runtime-asset-representation.md)
- [ADR-015：索引 Mesh 与渲染后端边界](decisions/ADR-015-indexed-rendering-boundary.md)
- [ADR-016：稳定类型标识与属性访问边界](decisions/ADR-016-stable-type-property-boundary.md)
- [ADR-017：编辑器 UI 与场景渲染组合边界](decisions/ADR-017-editor-ui-render-composition.md)
- [ADR-018：Editor 工程启动入口](decisions/ADR-018-editor-project-entry.md)
- [ADR-019：Editor 源资产、派生产物与导入索引边界](decisions/ADR-019-editor-asset-workflow.md)
- [ADR-020：Editor 场景创作与 Runtime 投影边界](decisions/ADR-020-scene-authoring-boundary.md)
- [ADR-023：公共 API 稳定级别与 1.x 兼容边界](decisions/ADR-023-public-api-stability.md)
- [ADR-024：Editor 与 Runtime 宿主进程隔离](decisions/ADR-024-editor-runtime-process-isolation.md)
- [ADR-025：原生游戏模块与 Engine 生命周期边界](decisions/ADR-025-native-game-module-boundary.md)
- [ADR-026：结构化日志事件与进程传输边界](decisions/ADR-026-structured-logging-transport.md)
- [ADR-027：Editor 使用单窗口 Docking 工作区](decisions/ADR-027-editor-ui-workspace.md)
- [ADR-028：Editor 与 Runtime 使用版本化本机 IPC](decisions/ADR-028-editor-runtime-ipc.md)
- [ADR-029：Runtime 检查使用只读镜像模型](decisions/ADR-029-runtime-inspection-model.md)
- [ADR-035：分离 IPC 传输与应用协议](decisions/ADR-035-ipc-transport-protocol-boundary.md)
- [ADR-036：资产热重载使用修订通知与事务替换](decisions/ADR-036-asset-hot-reload.md)

## 实施与验收记录

- [glTF 资产链与渲染优化最终验收记录](records/2026-08-27-gltf-pipeline-final-validation.md)
- [TOOL-001：glTF 资产编译器验收记录](records/TOOL-001-gltf-asset-compiler.md)
- [TOOL-002：Mesh Binary v1 实施记录](records/TOOL-002-mesh-binary-v1.md)
- [TOOL-003：索引渲染实施记录](records/TOOL-003-indexed-rendering.md)
- [静态几何 Arena 验收记录](records/2026-08-27-static-geometry-arena.md)
- [0.1.0 版本验收记录](records/0.1.0-validation.md)
- [0.3.0 版本验收记录](records/0.3.0-validation.md)
- [0.4.0 版本验收记录](records/0.4.0-validation.md)
- [0.5.0 版本验收记录](records/0.5.0-validation.md)
- [0.6.0 版本验收记录](records/0.6.0-validation.md)
- [0.7.0 编辑器基础验收记录](records/0.7.0-validation.md)
- [0.8.0 编辑器资产工作流验收记录](records/0.8.0-validation.md)
- [0.9.0 场景创作工作流验收记录](records/0.9.0-validation.md)
- [0.10.0 稳定性预览验收记录](records/0.10.0-validation.md)
- [0.11.0 Runtime 宿主工作流验收记录](records/0.11.0-validation.md)
- [0.12.0 游戏模块与生命周期验收记录](records/0.12.0-validation.md)
- [0.13.0 结构化日志与 Editor Console 验收记录](records/0.13.0-validation.md)
- [0.14.0 Editor UI 工作区候选验收记录](records/0.14.0-validation.md)
- [0.15.0 Editor–Runtime 双向控制通道验收记录](records/0.15.0-validation.md)
- [M-89 Editor UI Docking Spike 记录](records/M-89-editor-ui-docking-spike.md)
- [M-95 Editor–Runtime IPC 架构与协议 Spike 记录](records/M-95-editor-runtime-ipc-spike.md)
- [M-96 libuv I/O Core 实施记录](records/M-96-libuv-io-core.md)
- [M-97 IPC Transport 实施记录](records/M-97-ipc-transport.md)
- [M-98 Editor–Runtime 协议实施记录](records/M-98-editor-runtime-protocol.md)
- [M-99 Runtime 控制状态机实施记录](records/M-99-runtime-control-state-machine.md)
- [M-116 Prefab 格式、身份与组合 Spike 记录](records/M-116-prefab-format-identity-spike.md)
- [M-117 Prefab Loader 与依赖缓存实施记录](records/M-117-prefab-loader-cache.md)
- [M-118 Prefab 原子实例化与生命周期实施记录](records/M-118-prefab-runtime-instance.md)
- [M-119 场景格式 v3 与 Prefab 实例声明实施记录](records/M-119-scene-v3-prefab-instances.md)
- [M-120 Editor Prefab 放置与层级呈现实施记录](records/M-120-editor-prefab-placement.md)
- [M-121 Prefab 根级编辑与刷新事务实施记录](records/M-121-prefab-root-edit-refresh.md)
- [M-122 Lantern Gallery Prefab 复用实施记录](records/M-122-lantern-gallery-prefab.md)
- [M-123 Prefab 覆盖身份与合并语义实施记录](records/M-123-prefab-override-model.md)
- [M-124 场景 v4 Prefab 覆盖格式实施记录](records/M-124-scene-v4-prefab-overrides.md)
- [M-125 Prefab Runtime 覆盖投影实施记录](records/M-125-prefab-runtime-override-projection.md)
- [M-126 Editor Prefab 覆盖编辑实施记录](records/M-126-editor-prefab-override-editing.md)
- [M-127 Prefab 覆盖恢复与命令历史实施记录](records/M-127-prefab-override-revert-history.md)
- [M-128 Runtime 属性回写到 Prefab 覆盖实施记录](records/M-128-runtime-prefab-author-apply.md)
- [M-129 Lantern Gallery 差异化实例与跨平台验收记录](records/M-129-lantern-prefab-overrides-validation.md)
- [M-130 作者事务与来源修订 Spike 记录](records/M-130-author-transaction-spike.md)
- [M-131 Prefab 作者文档与原子保存实施记录](records/M-131-prefab-author-document-save.md)
- [M-132 从场景子树创建 Prefab 实施记录](records/M-132-create-prefab-from-subtree.md)
- [M-133 将实例覆盖应用到来源实施记录](records/M-133-apply-prefab-overrides.md)
- [M-134 Unpack Prefab 实例实施记录](records/M-134-unpack-prefab-instance.md)
- [M-135 Editor Prefab 作者操作实施记录](records/M-135-editor-prefab-author-actions.md)
- [M-136 Lantern Gallery Prefab 作者闭环验收记录](records/M-136-lantern-prefab-authoring-validation.md)
- [M-137 IPC 耦合审计与模块化决策记录](records/M-137-ipc-modularization-audit.md)
- [M-138 IPC v2 信封与请求关联实施记录](records/M-138-ipc-v2-envelope.md)
- [M-139 IPC 协议注册表与统一分发实施记录](records/M-139-ipc-dispatcher.md)
- [M-140 Session 与 Control 域协议实施记录](records/M-140-ipc-session-control.md)
- [M-141 IPC 数据域协议实施记录](records/M-141-ipc-data-domains.md)
- [M-142 IPC v2 会话组合实施记录](records/M-142-ipc-v2-session-composition.md)
- [M-143 IPC 兼容与故障验收记录](records/M-143-ipc-compatibility-validation.md)
- [M-144 资产热重载边界记录](records/M-144-asset-hot-reload-boundary.md)
- [M-145 工程源资产文件监听实施记录](records/M-145-asset-file-watcher.md)
- [M-146 自动重新导入队列实施记录](records/M-146-automatic-asset-reimport.md)
- [M-147 Asset IPC 协议域实施记录](records/M-147-asset-ipc-domain.md)
- [M-65 公共 API 与稳定性审计记录](records/M-65-public-api-audit.md)
- [M-66 稳定运行时代表性样例验收记录](records/M-66-stable-runtime-sample.md)
- [M-67 公共 API 与 ABI 加固记录](records/M-67-api-abi-hardening.md)
- [M-68 安装、升级与依赖消费验收记录](records/M-68-install-upgrade-validation.md)
- [M-69 性能与内存基线记录](records/M-69-performance-memory-baseline.md)
- [M-75 Runtime 进程失败与恢复验收记录](records/M-75-runtime-process-validation.md)
- [M-76 Runtime 示例与安装树阶段验收记录](records/M-76-runtime-install-validation.md)
- [M-13 JSON 解析器 Spike 记录](records/M-13-json-spike.md)
- [M-14 资产 URI 与资源生命周期实施记录](records/M-14-asset-uri-resource-lifecycle.md)
- [M-15 版本化场景 Schema 实施记录](records/M-15-scene-schema.md)
- [M-16 Mesh 与 Material Loader 实施记录](records/M-16-render-asset-loaders.md)
- [M-17 场景实例化与本地验收记录](records/M-17-scene-instantiation.md)
- [M-27 图片解码器 Spike 记录](records/M-27-image-decoder-spike.md)
- [M-31 工具能力重新评估记录](records/M-31-tooling-reevaluation.md)

## 文档维护

- [项目文档规范](../DOCUMENTATION_GUIDE.md)

其他教程、API 参考、ADR 和实施记录将在出现真实内容后加入本页。尚未实现的设计不是当前公共能力。
