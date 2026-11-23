<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景服务边界验收

日期：2026-11-05。环境：Windows、windows-clang-debug。
计划：[PLAN-101](../plans/PLAN-101-scene-service-boundary.md)。
决策：[ADR-106](../decisions/ADR-106-unified-asset-service.md)。

## 完成范围

- scene_service 移除 prepare_assets、poll_prepared_assets、discard_prepared_assets、
  request_assets、poll_assets、loading_assets 六个通用资源入口。
- Application 私有资源组直接请求 asset_manager，编辑器和 IPC 调用迁移到宿主入口；
  场景视觉加载保留独立组，均复用相同资产拥有者与弱缓存。没有新增资产服务。
- 宿主保留通用批次与场景切换/重载互斥；关闭时先释放批次，再关闭场景和资产管理器。
- scene_service 删除执行器指针；configure 保留实际用于统计与进度唤醒的 Render 和通知对象。
- request_scene/activate 直接返回请求编号/完成值；poll_scene/poll_reload 返回
  result<optional<完成值>>，区分调用错误、暂无回执和包含异步结果的终态。
- Application 既有轮询输出参数作为宿主适配保留，本次没有全项目迁移这些接口。
- 不再公开内部加载组，编辑器与 IPC 移除无用的 scene_service.hpp 包含。

## 验证

- 完整 Windows Clang Debug 构建通过。
- 42 项资产、Scene/Prefab、编辑器、渲染引用、内部头及分层回归全部通过（38.14 秒）。
- 另外运行 runtime.stop-protocol、editor.prefab-edit、editor.runtime-stop-failure、
  native-application-flow，4 项全部通过，共 46 项。
- 新断言验证场景服务不再暴露通用资源入口、Application 不公开加载组；
  场景轮询/取消不领取通用批次回执，批次在途时场景切换被拒绝。
- clang-format 检查与 git diff --check 通过。

日志：build/scene-boundary-build.log、build/scene-boundary-tests.log、
build/scene-boundary-extra-tests.log（不入库）。

## 限制

未运行大型场景、真实窗口人工交互、跨平台构建或远端 CI，未推送、合并或发布。
没有修改 World/ECS、同步实例化策略或 GPU 上传完成语义。
