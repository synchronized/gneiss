<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：来源重载回归迁移

日期：2026-11-11。

## 变更

Scene/Prefab 结构重载测试迁入 tests/host/runtime，目标随 Runtime 宿主构建，测试名称保持不变。
它们验证宿主来源重载策略；Engine 的纯组合、实例化和场景更新测试仍保留原归属。
async_scene_reload 测试辅助直接使用 runtime_scene_loader 和宿主回执，不再调用旧场景服务。
保留实体身份、覆盖规则、运行变换、多个场景基线、跨 World、卸载及视觉依赖失败断言；
视觉错误通过宿主日志验证，批次完成后消费日志队列。
迁移暴露新入口先检查就绪、后检查非法基线的问题，调整为先校验来源与场景归属。

## 验证

Windows Clang Debug 构建通过。runtime.scene-loader、scene_structural_reload、
prefab_structural_reload 三项回归通过，实际源码分层检查和 git diff --check 通过。
未运行大场景、窗口 GPU、跨平台或远端 CI。

## 剩余

[PLAN-113](../plans/PLAN-113-application-scene-boundaries.md) 尚有 native_application、
scene_load_application、scene_load_baseline、main_loop_response、asset_pixel 的旧入口需要迁移。
scene_service/runtime/loading 及 Application 装配暂未删除。
