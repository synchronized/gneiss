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
- [C/C++ 代码风格与语言标准](guides/coding-style.md)
- [在 Editor 中导入资产](guides/editor-assets.md)

## API 参考

- [Core 版本与结果接口](reference/core.md)
- [RID 有效性与 Service 生命周期](reference/rid.md)
- [World、Entity 与内部 ECS 边界](reference/world.md)
- [Scene Tree、Entity 映射与 Transform](reference/scene-tree.md)
- [Application 生命周期、时间与主循环](reference/application.md)
- [输入事件与状态快照](reference/input.md)
- [输入动作映射格式 v1](reference/input-map-format.md)
- [诊断回调](reference/diagnostics.md)
- [Type Registry 与反射元数据](reference/reflection.md)
- [Render 资源、ECS 组件与帧提取](reference/render.md)
- [资产 URI、目录挂载与缓存](reference/assets.md)
- [场景文件格式 v2](reference/scene-format.md)
- [场景加载、实例与卸载](reference/scene-instance.md)
- [工程文件格式 v1](reference/project-format.md)
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
