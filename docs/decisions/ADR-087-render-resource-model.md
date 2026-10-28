<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-087：类型化渲染资源与服务生命周期

- 状态：已接受（本轮实现及编译完成，运行验收状态见 PLAN-054）。
- 日期：2026-10-26。

## 背景

目前运行资源身份、CPU 负载、材质依赖和 GPU 对象分布在缓存、Loader 隐藏对象、
render_resource_service 与 render_service 中。流送尚未开始，需要先明确所有权，
避免用长期大块 CPU 数据代表资源身份，也避免重新引入版本发布及事务协议。

## 决策

采用类型化 mesh_resource、texture_resource、material_resource 和现有 asset_ref，
用 mesh、texture、material 别名统一上层引用。缓存不强制类型擦除，不增加无真实用途的通用基类。
上层运行对象保存稳定身份、轻量元信息和依赖，Render 私有实现管理 RID 对应的后端记录。
可保存的 description 与上传用 data 分开；不是每类资源都必须增加对称类型层级。

功能层 asset_manager 管理统一来源/URI/类型索引、加载调度和缓存，不依赖 Render 后端实现。
其私有加载实现通过 render_service 的 RID 接口操作渲染内容表。
ECS 和上层帧输入允许持有引擎资源强引用，内部 RID 留在 Render 边界；
ECS 必须支持非平凡组件生命周期，序列化仍使用资源标识。场景节点仍通过实体关联组件，
不直接持有服务或第三方后端对象。替代原“ECS 仅纯值或 RID”的限制。

普通加载完成和进度只覆盖本次请求约定的 CPU 内容，不等待或传播 GPU 上传进度。
Render 保留上传完成、失败、暂存数据和后端可用性管理。
资源身份与内容可用性分离。重载允许旧内容继续绘制，仅保留单资源在途更新，
不建立候选 RID、内容版本树和跨资源事务。请求序号仅用于拒绝迟到结果。

渲染运行资源使用自定义删除器，在最后释放时无分配地入队，由主线程消费并析构。
纯 CPU 数据仍使用普通 RAII，不强制所有 asset_ref 延迟销毁。
最后一个上层引用释放不等于可以立即销毁 GPU 对象；实际 GPU 回收仍由 Render 所属线程和完成机制负责。
关闭时停止生产并排空级联销毁；端点 detached 后，残留引用可在任意线程销毁无服务依赖的 CPU 外壳。
入队与关闭须同步，不能留下无人消费的对象，也不能访问已析构服务。
本轮保留 CPU 恢复数据，按需释放及流送另行推进。

## 资源目录与 Render 接口边界（2026-10-28 补充）

类型化运行资源放在 `engine/resource/{mesh,texture,material}`，上层继续使用强引用。
资源头不包含 Render 实现；资源自身析构不调用 Render，RID 释放由延迟销毁队列负责。
长期目标是 `engine/function/render` 只保留渲染实现及服务边界，上层资源通过
`render_service` 的 RID 接口创建、更新和销毁渲染端记录。普通业务不改回 RID 所有权。
`resource_service::implementation` 在功能层协调资源对象与 Render RID 接口，
不归入渲染服务内部。资源值与编解码器不得反向依赖功能层。Application 拥有 asset_manager、resource_service 和 Render，资源服务内部直接拥有资源包装索引和销毁队列，
服务私有实现管理资源包装及销毁队列，且必须先于 Render 关闭和析构；Render 不拥有上层资源实现、不回调上层对象。
更新方向固定为功能层准备资源依赖 → Render 接受 CPU 数据 → 服务私有实现更新资源元信息和强引用。
材质更新直接使用 Loader 已取得的纹理强引用，不由 Render 从 RID 重建依赖。
Render 私有表只保存 RID 对应内容；不新增 URI 缓存或重复控制块。
宿主允许包含管理器和服务接口，但不能包含资源服务私有实现、加载实现、RID 表或资源私有访问器；边界检查覆盖这些限制。
服务只为自身新建 RID 构造资源对象，不提供外部 RID 接管入口；共享通过复制 asset_ref 完成。
RID 查询仅锁定本服务已有弱引用。材质创建和更新均显式传入纹理强引用，不重建拥有者。
关闭时先排空队列，只销毁仍有效的记录；已失效 RID 不再重复提交销毁请求。
实施及验收进度见 PLAN-054。

## 影响

收益：运行身份和依赖可直接理解，生命周期入口减少，未来流送可改变驻留而不改变业务引用。
成本：需要贯通 Loader、场景、材质依赖及帧提交迁移，并验证关闭和跨线程边界。
内部后端记录仍然必要；本决策不承诺消除所有数据转换或 GPU 上传暂存。

## 替代方案

- 维持通用 entry 引用和隐藏所有者：改动少，但继续分散依赖和析构规则。
- 全部改为直接引用 Granit 对象：调用简单，但违反 ECS/后端隔离并暴露后端生命周期。
- 立即引入完整流送和持久化渲染场景：覆盖面大，但当前需求不足，增加未验证的状态与协议。

实施步骤、验收和待落实细节以 [PLAN-054](../plans/PLAN-054-render-resource-model.md) 为唯一来源。

## 功能层加载与运行资源服务

asset_manager 负责异步 CPU 数据加载、解码注册、请求合并与 CPU 缓存，不依赖 Render。
resource_service 借用 asset_manager 和 render_service，拥有运行资源索引、批次装配
和释放队列。Scene 和程序化创建调用 resource_service；描述加载调用 asset_manager。
两者缓存对象不同，同一个运行资源只由 resource_service 登记。资源值和格式编解码仍在
engine/resource，不反向依赖服务。GPU 上传独立于 CPU 内容采用完成。

资源服务停止不关闭共享的 asset_manager 或文件服务；宿主先停止资源批次、释放场景，
销毁资源服务，再销毁资产管理器和 Render。服务不增加 Loader 转发层、事务或 GPU 等待协议。
本决策替代此前由 asset_manager 同时拥有加载与运行资源创建的方案。
