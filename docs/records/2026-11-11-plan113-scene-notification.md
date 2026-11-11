<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：场景资产完成通知收敛

日期：2026-11-11。完整计划尚未完成。

## 变更

场景打开与来源重载订阅既有资产完成通知，就绪后才领取结果，移除逐帧尝试 take 的行为。
通知通过弱引用更新原子标记，不捕获服务或任务裸指针；主循环仍检查就绪标记。
删除加载进度及私有提交接口中固定为 1 的 session/revision，真实 IPC 版本仍由宿主管理。

## 验证

Windows Clang Debug 构建通过，实际源码分层检查通过。8 项定向回归通过：
editor.scene-loader、native-application、scene-load-application、scene-load-preparation、
scene_structural_reload、prefab_structural_reload、editor.asset-scene-commands、
editor.document-editing。git diff --check 通过。
未运行大场景、窗口 GPU、跨平台或远端 CI。

## 剩余

宿主直接使用资产请求、组合与实例化，以及删除 scene_service/runtime/loading，仍属
[PLAN-113](../plans/PLAN-113-application-scene-boundaries.md) 未完成工作。
