<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# World 与 Entity

## World 生命周期

`gneiss::engine::create_native()` 返回 `result<world_owner>`，成功值独占一个新 World。
每次调用创建独立 World，不接收已有拥有者；调用方成功后可显式移动拥有权。
拥有者在创建线程释放时销毁 World 及全部实体、组件。
World 不再登记全局 RID 表，不提供借用 RID 或 world_service 转发接口。
工厂仅分配节点与实体校验所需的唯一所属域，域编号不会复用。

World 及其实体只能在创建线程访问；跨线程操作返回 `error::invalid_state`。
内部直接借用 world&；Application 的 native_world() 返回活动对象的借用指针。
场景切换或关闭后必须重新获取，禁止解引用旧指针；借用者必须在 World 销毁前清理。
反射组件入口也直接接收 world&，不再按句柄查回对象。

## Entity 生命周期

`world::entity_create` 返回运行时 `gneiss::entity_id`。实体 ID 包含 World domain 和 EnTT
实体 generation，但编码布局不是公共 ABI：

- 零值表示无效实体。
- 实体只能用于创建它的 World。
- 销毁后旧 ID 失效，即使底层槽位被复用也不会指向新实体。
- 实体 ID 不能持久化为场景对象身份。

`world::entity_is_alive` 返回 `result<bool>`，对已销毁或其他 World 的实体返回成功值 false；需要执行销毁、
读写组件等操作时，这类实体返回 `error::invalid_handle`。


`world::entity_count()` 返回 `result<std::uint64_t>`；成功的零值与跨线程访问失败分别表达。

## Game Object 借用

`world::create_object(identity, transform, parent)` 创建有层级的实体并返回 `result<game_object>`。
`world::object(entity)` 借用已有对象，纯 ECS 实体返回 not_found；失效或跨 World 实体返回 invalid_handle。
`game_object` 只保存 world* 和 entity_id，复制不保活实体，不保存组件地址。
World 必须比所有句柄的使用期更长；World 销毁后禁止再调用句柄。

identity、local_transform、world_transform 和 parent 返回值或结果；set_identity、
set_local_transform、reparent 和 destroy 修改 World 中的同一实体。
根节点的 parent 返回成功的空句柄。reparent 接受空父节点、拒绝跨 World 和循环关系。
重复指定同一父节点是无副作用操作。destroy 删除整棵子树，复制的借用句柄同步失效。
场景创建已使用该入口；旧 node_* 调用方仍在迁移中。

## ECS 与 System

EnTT `3.15.0` 只用于 World 的内部组件存储。Gneiss 公共头、Entity ID 和 C ABI 不依赖 EnTT。
内部 System Scheduler 按注册顺序串行执行，遇到第一个失败结果立即停止并保留该结果。

Camera 与 Mesh Renderer 是当前原生组件，其行为由
[Render 资源、组件与帧提取](render.md)定义。Transform 和 Camera 可通过
[Type Registry 与反射元数据](reflection.md)注册并按 World、Entity、Type GUID 和 Field ID 访问；
公共接口不暴露 EnTT 或组件地址。可持久化对象 UUID 仍由场景格式负责，不使用运行时实体 ID。
