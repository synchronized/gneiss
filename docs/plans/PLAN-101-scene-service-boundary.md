<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-101：场景生命周期与通用资源批次边界

状态：已完成，Windows 编译及 46 项相关回归通过。日期：2026-11-05。归属：[DEV-052](DEV-052-architecture-cleanup.md)。
架构依据：[ADR-106](../decisions/ADR-106-unified-asset-service.md)。

## 目标与边界

scene_service 只协调活动场景、结构准备、实例重载、视觉加载与退场。
通用资源批次由 Application 私有的现有 resource_load_group 汇总，直接请求 asset_manager。
场景自身使用独立的同类请求组；两组均不拥有 URI 缓存，不新建资产服务。
宿主保留场景切换与编辑批次之间的互斥，避免异步编辑结果跨场景应用。

不改 World/ECS，不改变场景同步实例化，不引入 GPU 等待或跨资源事务。
Application 既有轮询适配保持行为；本轮值返回统一针对 scene_service。

## 实施顺序

- [x] 移出六个通用资源接口，迁移编辑器、IPC 与测试调用。
- [x] 删除 scene_service 的执行器借用；保留实际用于统计的 Render 与进度通知。
- [x] request_scene/activate 值返回；poll_scene/poll_reload 使用 result<optional<完成值>>，
  外层错误表示调用失败，空值表示暂无完成结果，完成值携带异步工作结果。
- [x] 验证关闭时请求观察者释放、场景切换互斥、独立取消及编辑器迟到结果处理。
- [x] 完整编译、相关回归、格式与边界检查，记录结果并本地提交。

## 验收与限制

覆盖场景加载/取消/激活、结构重载、资源批次、编辑器资产/Prefab 编辑、撤销和保存、宿主关闭。
不运行大场景、跨平台、远端 CI 或发布。请求编号只在所属请求组内有效，不跨组使用。

实施结果见[验收记录](../records/2026-11-05-scene-service-boundary.md)。
