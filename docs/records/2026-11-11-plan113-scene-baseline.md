<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：场景加载基准迁移

日期：2026-11-11。

## 变更

scene_load_baseline 的预载、初次加载、连续切换、取消及失败重试均改用资产请求，
通过测试辅助组合和实例化，显式消费视觉批次。删除旧场景完成回执和阶段统计依赖。
保留旧场景、图像一致性、资源计数、交互响应、重复切换和关闭任务回收检查。
失败夹具由缺失材质改为损坏场景 JSON，以符合结构失败与视觉降级的边界。

统计与取消口径见[构建指南](../guides/building.md#真实大场景测量)。测量脚本同步删除
不再实现的候选内存、上传预算和回滚断言，不再把缺失字段默认为零；
取消检查改为请求观察者释放，不将其描述为后台任务或 GPU 清理完成。

## 验证

Windows Clang Debug 的 gneiss_scene_load_baseline 编译通过。
两个 Python 测量脚本语法检查通过；生命周期校验器使用各场景合成报告检查成功路径，
并确认请求未释放、关闭后任务残留仍被报告为失败。
源码边界检查及 git diff --check 通过。
未运行窗口、大场景、GPU 基准或远端 CI，因此不提供新的性能数据或像素验收结论。

## 剩余

[PLAN-113](../plans/PLAN-113-application-scene-boundaries.md) 尚有
scene_load_application_test 依赖旧服务；旧服务实现及 Application 装配未删除。
