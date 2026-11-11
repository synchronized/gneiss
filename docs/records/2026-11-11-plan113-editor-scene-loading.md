<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：编辑器直接加载场景资产

日期：2026-11-11。

## 变更

编辑器不再调用 scene_service::open/activate，而是订阅场景资产完成，建立文档快照，
组合并实例化到 Application 的既有 World。成功后由编辑会话接管新场景并关闭旧文档。
失败和取消保留原文档、选择及撤销历史；创建的新场景在绑定失败时明确卸载。
视觉资源批次由编辑器加载器持有，终态失败记录日志，不阻止场景编辑。
不再创建 Runtime 的 scene_load_completion 或来源重载基线；UI 进度暂沿用迁移期值类型。

## 验证

Windows Clang Debug 构建、实际源码分层检查、git diff --check 通过。
7 项定向回归通过：runtime.smoke、editor.scene-loader、editor.runtime-scene-loading、
editor.asset-scene-commands、editor.document-editing、editor.document-actions、
editor.scene-document-snapshot。
加载测试覆盖线程池和协作执行、失败保留历史、取消、重复切换、Prefab 文档恢复，
并断言编辑器请求不占用 scene_service 的加载状态。
资产编辑测试最初仍等待旧服务的视觉状态而提前启动编辑；改为等待编辑器持有的批次后通过。
未运行大场景、窗口 GPU、跨平台或远端 CI。

## 剩余

[PLAN-113](../plans/PLAN-113-application-scene-boundaries.md) 的两类宿主打开入口已迁移。
来源重载、测试及示例旧调用方、共享回执类型和 scene_service/runtime/loading 删除尚未完成。
