<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 统一资产服务验收

日期：2026-11-05。环境：Windows、windows-clang-debug。
计划：[PLAN-100](../plans/PLAN-100-unified-asset-service.md)。
决策：[ADR-106](../decisions/ADR-106-unified-asset-service.md)。

## 完成范围

- 合并 asset_manager 与 resource_service，删除后者及 Application.resources()；只有一个资产拥有者和最终资产弱索引。
- 统一 asset_request<T>，删除 resource_request 和第二套请求状态。类型化加载直接领取对应资源引用；混合列表沿用既有资源条目。
- 纹理、网格及材质装配的 CPU 中间产物只由任务持有；显式 CPU 数据请求仍可独立加载，按类型隔离。
- cache() 改为只读 cached_count()；最终渲染资源身份包含完整 profile，显式不匹配参数不会误命中。
- Scene/Prefab 结构准备迁入 asset_manager.prepare_scene，运行层删除独立准备 scope；保留同步实例化、活动场景、退场及视觉后续加载。
- 宿主统一配置资产执行器，文件完成、资产推进、场景/渲染推进各自负责自身阶段。
- 统一停止拒绝所有新请求；描述旧引用继续有效，Render RID 销毁及晚释放仍沿用所属线程退休队列。

当前接口见[资产加载与缓存](../reference/assets.md)。resource_cache 保留原文件名用于现有混合条目与独立缓存测试，未新建第二套索引类。

## 验证结果

- `cmake --build --preset windows-clang-debug -j5` 通过，包含 Engine、Runtime、Editor、工具与测试目标。
- 36 项针对性回归全部通过，最终一轮 31.35 秒。
- 覆盖通用资产管理、读取合并、取消、失败诊断、重载、材质纹理依赖、晚释放和销毁队列。
- 覆盖 Scene/Prefab 准备、实例化、结构重载、编辑器打开/保存/属性与组件编辑、材质编辑和 Prefab 作者操作。
- 分层正向/负向检查及内部头可见性通过。
- 新用例验证类型化纹理请求 take、CPU 中间负载不进入缓存、profile 不匹配、无 Render 场景准备、独立取消、统一停止及停止后的描述引用。
- 修改的 C++ 文件 clang-format 检查及 git diff --check 通过。

本地日志：`build/plan100-final-build.log`、`build/plan100-final-tests.log`（不入库）。

## 未验证范围与限制

未运行大型场景、跨平台构建、远端 CI 或发布；未执行远端及 Granit 仓库操作。
本次没有引入流送、跨资产事务或任意动态依赖图；材质/纹理及 Scene/Prefab 使用各自明确的依赖规则。
运行资源完成仍只表示 CPU 内容已采用，不表示 GPU 上传完成。
