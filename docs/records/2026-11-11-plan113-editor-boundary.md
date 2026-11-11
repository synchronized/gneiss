<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113 第一组：编辑职责与 Transform 标识迁移

日期：2026-11-11。范围为 [PLAN-113](../plans/PLAN-113-application-scene-boundaries.md) 第一组，
不代表完整计划完成。

## 变更

- scene_mesh_binding 移入 editor/documents/mesh_binding.hpp 和 gneiss::editor，
  Application 删除无用包含；引擎接口清单不再导出编辑资源保活结构。
- Prefab 组件、变换、结构编辑候选及网格 URI 覆盖生成移入 editor/documents/prefab_edit。
  编辑实现编入 gneiss_scene_document；组合、覆盖校验和运行回放仍留在引擎。
- 通用 set_prefab_property_override 继续作为覆盖数据的校验、规范化与更新原语，
  不持有编辑器状态，也不生成组件编辑候选。本批不复制其校验算法。
- 删除 component_identity.hpp：类型 GUID 直接使用 math::transform::type_guid，
  属性编号集中到 core/meta/transform_properties.hpp，注册和路径构造共用同一声明。
- 同步编辑器、宿主、示例、测试和内部头清单；运行回放头保留供编辑器复用。

## 验证

- windows-clang-debug 全量构建通过，包含编辑器、宿主、示例和测试目标。
- 定向测试 7/7：editor.prefab-edit、prefab_description、prefab_property_override、
  internal-header-visibility、editor.component-edit、editor.prefab-component、
  editor.prefab-author-operations。
- check_core_boundary.cmake、git diff --check 通过。
- 首轮构建发现迁移后的编辑器包含根、示例旧头和一处旧类型限定；修复后增量构建通过。
- 未运行大场景、完整 GPU 矩阵、跨平台和远端 CI。

## 后续

继续 PLAN-113 第二组资产批次收敛；当前仍由 scene_service 管理活动 World，
Application 直接拥有 World 的目标尚未落地。
