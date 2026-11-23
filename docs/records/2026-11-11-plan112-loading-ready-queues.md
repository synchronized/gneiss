<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-112：文件、任务及资产就绪队列验收

2026-11-11，Windows Clang Debug，本地验证。

## 实施结果

- 文件服务：完成后投递请求 ID，所属线程按预算消费，处理回退并交付回调，不扫描全部 pending 请求。
- 任务调度器：终态加入 scope 完成队列，poll 按入队顺序领取，不扫描全部任务表。
- 资产管理器：读取、解码、依赖完成及请求释放触发就绪通知；重复入队合并，任务回执按 ID 定位操作。
- 活跃表负责所有权，队列持有弱引用；服务关闭后的迟到通知不会访问已销毁服务。
- collect_unused 保留立即释放无观察者渲染操作和 RID 的语义，不推进加载阶段。

## 验证

- windows-clang-debug 全部目标编译通过，补充用例后的增量编译通过。
- 21 项定向 CPU 测试通过（包含失败修正后的定向重跑）：
  task-scheduler、files.service/native、assets.application/manager/material、asset_manager、
  render_resource_service、render_asset_preparation、render_asset_loader、texture-load-service、
  texture-load-application、scene-load-application、scene-load-preparation、prefab-preparation、
  prefab-runtime-application、prefab_structural_reload、native-application、
  editor.asset-edit/prefab-edit/scene-loader。
- 新增测试覆盖文件和任务完成顺序不同于提交顺序、零预算和逐项领取、scope 隔离、重复通知合并、
  等待项不逐帧恢复、取消后回收、队列销毁后的迟到通知，以及 CPU 提交容量耗尽后的恢复。
- check_core_boundary.cmake、test_core_boundary.cmake、git diff --check 通过。

首轮回归发现 collect_unused 将立即回收推迟，已恢复原有所有权清理语义。
新增容量测试最初设置的容量同时限制 scope 数量，导致配置失败；调整为两个 scope、两个占位任务后通过。

## 保留边界

任务选择、定时与串行调度仍使用原实现；后端事件泵仍需驱动。
容量不足的提交使用独立重试集合；弱缓存、资源退休及失效后端映射仍由维护入口处理。
场景、编辑批次与输入配置未在本次改为完成通知，不宣称整个引擎已消除轮询。
未运行 GPU、大场景、跨平台或远端 CI，未进行性能基准或远端发布。

计划见 [PLAN-112](../plans/PLAN-112-loading-ready-queues.md)。
