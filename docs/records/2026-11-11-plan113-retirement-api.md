<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：删除场景退休统计

日期：2026-11-11。延续 World 所有权迁移，完整计划尚未完成。

## 变更

删除 Application::scene_retirement、scene_service::retirement 及统计结构/成员。
World 已不再替换，原退休时间、数量和 pending 字段没有有效状态来源。
监测程序与资源数量断言直接使用 render_service::live_resource_count；报告不再输出
maximum_retirement_ms 和 retirement_ms。编辑器场景加载注释同步到持久 World 行为。

## 验证

Windows Clang Debug 构建通过；6 项小型回归通过：assets.application、scene-load-application、
scene-load-preparation、texture-load-application、scene_instance、prefab-runtime-application。
分层检查及 git diff --check 通过。渲染监测程序完成编译，未执行大场景、GPU 基准、跨平台或远端 CI。

## 剩余

Runtime/Editor 的 open/activate 调用和 Runtime 来源重载仍依赖 scene_service。
下一步迁移宿主场景切换策略、来源状态与调用链，再删除该协调器，不能仅通过改名视为完成。
