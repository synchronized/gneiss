<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：来源重载基线交给调用方

日期：2026-11-11。第三阶段继续实施，完整计划尚未完成。

## 变更

删除 scene_service 长期保存的 source_。激活回执携带 scene_reload_state 引用，由调用方按需
保留，重载显式传入；异步请求仅在执行期间持有该引用。基线记录借用 World、实例 ID、
不可变来源和对象地址关联，不拥有 World。Runtime 保存当前场景基线，编辑器继续以文档投影为准。
不再因激活另一个场景覆盖旧场景的重载基线；当前协调器仍串行执行重载。
原有 apply_source 差异和运行变换保留规则未改变。空基线、跨 World、失去同步或已卸载实例被拒绝。

## 验证

Windows Clang Debug 全量及最终增量构建通过。10 项定向回归通过：editor.scene-loader、
native-application、scene-load-application、scene-load-preparation、scene_instance、
scene_structural_reload、prefab-runtime-application、prefab_structural_reload、
editor.asset-scene-commands、editor.document-editing。
新增第二场景加载后重载第一场景、空基线与跨 World 拒绝、卸载后重载拒绝和其他实例保留检查。
分层检查与 git diff --check 通过。未运行大场景、窗口 GPU、跨平台或远端 CI。

## 剩余

基线所有权已迁移，但来源更新算法、异步重载协调和打开/激活协议仍在 scene_service。
后续迁移这些调用链并删除服务，随后整理 application/scene_update/render_extraction 目录与名称。
