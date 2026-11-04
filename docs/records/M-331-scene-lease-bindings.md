<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：场景构建消费选定租约

2026-10-15，继续 [共享资产方案](../plans/asset-scene-ownership.md)。用户确认的热重载更新
方向已写入 [ADR-064](../decisions/ADR-064-shared-assets-and-scene-instances.md)。

## 实现

scene_load_service 收集资产完成结果中的租约，形成单次请求的绑定集合；重复 URI 必须
指向同类型、同 RID，否则拒绝候选。构建开始时将集合移入 scene_load_builder。
该集合仅保存已选定的使用引用，不是全局缓存，也不解析“最新版本”。

场景和 Prefab 的 create_staged_node 接收 Mesh、Material 强类型租约，移入节点所有权。
移除分阶段构建里的 acquire_cached、acquire_mesh 和 acquire_material；缺失租约返回
NOT_READY，不执行同步读取。构建器结束后，节点继续持有租约。
通用渲染租约增加 URI 查询及类型安全转换，类型错误返回空租约。

当前场景请求仍使用旧资产发布模式，但构建路径已可消费固定版本租约。不能只开启固定
模式而遗漏活动场景热重载更新；本次没有宣称完成该切换。

## 验证

Windows Clang Debug 构建成功，定向 CTest 8/8 通过：scene-load-builder、scene-load-application、
scene_instance、scene_prefab_instance、scene_structural_reload、prefab_structural_reload、
core-boundary、core-boundary-negative。

构建器测试使用固定版本 Mesh 和 Material，URI Mesh 缓存保持未命中；选定绿色材质后又
在旧 URI 缓存加载蓝色材质，仍能完成普通节点和 Prefab 构建且不增加文件读取。
构建器及外部材质租约释放后，节点继续保持绿色版本存活。缺失绑定时回滚候选且无同步读取。
原结构热重载与活动场景隔离测试通过。

构建器和场景加载服务已执行 clang-tidy，加载服务仍报告复杂度等告警；不宣称全文件静态
检查通过。未运行真实 GPU、完整 Sponza 或跨平台发布矩阵，未进行远端操作。

## 剩余工作

实现活动 World 的使用者更新事务，再启用场景固定版本请求；继续迁移 Application 级
共享资产所有权。更新通知、帧边界引用切换和共享任务取消隔离仍待实现。
