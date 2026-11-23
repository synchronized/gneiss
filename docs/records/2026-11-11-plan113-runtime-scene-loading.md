<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：Runtime 场景加载直接调用资产与实例化

日期：2026-11-11。

## 变更

Runtime 场景打开移除 scene_service::open/activate，直接订阅场景资产完成，随后组合内容，
在 Application 的既有 World 创建场景并实例化。宿主在成功后卸载原实例。
取消放弃资产观察者；失败保留原场景，迟到通知通过弱引用失效。
视觉加载使用既有共享批次，宿主持有观察句柄；失败记录日志。冒烟退出及资产重载检查该批次。
来源重载及回执类型仍沿用迁移期协议，编辑器打开入口尚待迁移；未新增通用协调服务。

## 验证

Windows Clang Debug 构建、实际源码分层检查、git diff --check 通过。
6 项定向回归通过：runtime.smoke、editor.runtime-scene-loading、editor.scene-loader、
scene_structural_reload、scene-instantiation、prefab_structural_reload。
IPC 回归覆盖首次加载、缺失文件、修复重试、取消后重试和关闭。
未运行大场景、跨平台与远端 CI。

## 剩余

[PLAN-113](../plans/PLAN-113-application-scene-boundaries.md) 尚未完成：
编辑器入口、来源重载及旧 scene_service/runtime/loading 清理仍待推进。
