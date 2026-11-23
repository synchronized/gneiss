<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：渲染提取归入前端

日期：2026-11-11。完整计划尚未完成。

## 变更

render_extraction.hpp/.cpp 从 function/scene 移至 function/render，删除旧路径，同步 Application、
测试、CMake 和内部接口清单。原文件中的 set/get_active_camera 实现拆至 scene/camera_selection.cpp，
活动相机仍归 World 场景状态管理。提取接口、资源强引用与 ECS 借用规则不变。

分层检查仅允许 render_extraction.cpp 作为读取 World/Scene 的渲染前端桥接；不允许它依赖 GPU
后端，也不允许其他渲染实现反向包含场景。新增桥接正例及后端、Application、C TRS 反例。

## 验证

Windows Clang Debug 构建通过；8 项定向回归通过：editor.scene-loader、editor.camera、
scene_tree_transform、render_snapshot、camera_math、scene-load-application、scene_instance、
scene_structural_reload。完整分层反例、实际源码边界检查与 git diff --check 通过。
未运行大场景、窗口 GPU、跨平台或远端 CI。

## 剩余

scene_service 的打开/激活/重载异步协调仍待迁移；application 目录迁移和计划最终验收尚未完成。
