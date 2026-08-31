<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 架构决策

本目录保存已经接受的长期架构决策。ADR 记录决策发生时的背景、选择和影响；当前接口行为仍以
Reference 和实现为准。

- [ADR-001：Granit 依赖接入边界（已取代）](ADR-001-granit-dependency.md)
- [ADR-002：ECS、反射与序列化边界](ADR-002-ecs-reflection-boundary.md)
- [ADR-003：Granit 依赖来源策略](ADR-003-granit-provider-strategy.md)
- [ADR-004：使用 yyjson 解析内部 JSON 文档](ADR-004-json-parser.md)
- [ADR-005：资产 URI 与虚拟文件系统边界](ADR-005-asset-uri.md)
- [ADR-006：场景 Schema v1 持久化契约](ADR-006-scene-schema-v1.md)
- [ADR-007：首版 Mesh 与 Material 运行时资产格式](ADR-007-render-asset-formats.md)
- [ADR-008：动作标识与帧状态折叠](ADR-008-action-identity-and-folding.md)
- [ADR-009：同步诊断回调](ADR-009-synchronous-diagnostics.md)
- [ADR-010：纹理资源与后端镜像边界](ADR-010-texture-resource-boundary.md)
- [ADR-011：使用 libspng 解码首版 PNG 纹理](ADR-011-png-decoder.md)
- [ADR-012：3D 坐标、Camera 与后端变换边界](ADR-012-3d-camera-coordinate-boundary.md)
- [ADR-013：glTF 离线导入与工具边界](ADR-013-gltf-import-boundary.md)
- [ADR-014：源资产、作者格式与 Runtime 二进制资产分层](ADR-014-runtime-asset-representation.md)
- [ADR-015：索引 Mesh 与渲染后端边界](ADR-015-indexed-rendering-boundary.md)
- [ADR-016：稳定类型标识与属性访问边界](ADR-016-stable-type-property-boundary.md)
- [ADR-017：编辑器 UI 与场景渲染组合边界](ADR-017-editor-ui-render-composition.md)
- [ADR-018：Editor 以版本化工程描述作为启动入口](ADR-018-editor-project-entry.md)
- [ADR-019：Editor 源资产、派生产物与导入索引边界](ADR-019-editor-asset-workflow.md)
- [ADR-020：Editor 场景创作与 Runtime 投影边界](ADR-020-scene-authoring-boundary.md)
- [ADR-021：Transform Gizmo 与显式 TRS 边界](ADR-021-transform-gizmo-boundary.md)
- [ADR-022：逐帧 Debug Draw 边界](ADR-022-debug-draw-boundary.md)
- [ADR-023：公共 API 稳定级别与 1.x 兼容边界](ADR-023-public-api-stability.md)
- [ADR-024：Editor 与 Player 进程隔离](ADR-024-editor-player-process-isolation.md)
