<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：宿主加载回执与编辑器状态清理

日期：2026-11-11。

## 变更

编辑器删除 Runtime 加载进度依赖，使用五态 scene_open_state；active 从状态推导，
移除重复布尔状态、未使用内存账本、计数及清理标记。界面显示不定进度，支持提交前取消。
Runtime 在宿主头中定义精简的打开回执、来源基线及重载回执，删除无用资源绑定副本和
未填写的耗时字段；日志仅保留实际采样的 inspection_rebind_ms。
src/host 和 src/editor 已无对 runtime/loading 或其加载类型的直接引用。
Application 仍包含旧服务，旧类型暂为测试和服务实现保留，不表示整个 runtime 目录已删除。

## 验证

Windows Clang Debug 构建、实际源码分层检查及 git diff --check 通过。
6 项回归通过：runtime.scene-loader、runtime.smoke、editor.scene-loader、
editor.runtime-scene-loading、editor.asset-scene-commands、editor.document-editing。
编辑器测试新增五态检查和请求提交前取消，保留在途取消、失败保留文档与历史的覆盖。
未运行窗口 UI、大场景、跨平台或远端 CI。

## 剩余

[PLAN-113](../plans/PLAN-113-application-scene-boundaries.md) 仍需迁移旧测试与测试回执，
删除 scene_service/runtime/loading 及 Application 对旧服务的装配，并完成统一验收。
