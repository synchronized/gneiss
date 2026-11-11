<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：Application 目录归属整理

日期：2026-11-11。完整计划尚未完成。

## 变更

Application 主类迁入 src/application，配置移到主类旁，main_loop_wakeup 移入 detail。
命名空间保持 gneiss::runtime，不保留旧路径兼容头。同步调用方、CMake、接口清单和文档。
分层检查覆盖新 Application 目录，拒绝 Engine 反向依赖以及 Application 依赖 Editor/Host。
src/runtime/loading 暂留待删除的场景协调，不标为已迁移完成。

## 验证

Windows Clang Debug 全量及最终增量构建通过；完整分层反例与实际源码检查通过。
8 项定向回归最终通过：editor.scene-loader、main-loop-wakeup、application_log、
native-application-flow、native-application、scene-load-application、texture-load-application、
prefab-runtime-application。唤醒测试原先假定异常转失败及回执按提交顺序返回；根据已落地的
致命异常策略和完成队列行为，改用显式失败结果并按任务身份核验终态，不修改调度器行为。
git diff --check 通过。未运行大场景、窗口 GPU、跨平台或远端 CI。

## 剩余

场景打开/激活/重载的异步协调和旧 scene_service 协议仍待迁移；最终统一验收未完成。
