<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# World 与 Entity

## World 生命周期

`gneiss::engine::create_native(owner, borrowed)` 创建空 World，返回唯一拥有者 `world_owner`
和只借用的 RID。拥有者在创建线程释放时销毁 World 及全部实体、组件，并撤销借用 RID。
工厂失败不替换已有 owner，输出 RID 置零；Application 拥有的 World 不授予调用方销毁权。
完整 World C API 与版本化创建描述已经删除。

World 及其实体只能在创建线程访问；跨线程操作返回 `result::invalid_state`。
拥有者有效期间直接调用原生 `world` 成员，不能在销毁后访问借用指针。
内部注册表验证 RID 的 domain、generation 和线程归属，失效 RID 返回 `invalid_handle`。

## Entity 生命周期

`world::entity_create` 返回运行时 `gneiss::entity_id`。实体 ID 包含 World domain 和 EnTT
实体 generation，但编码布局不是公共 ABI：

- 零值表示无效实体。
- 实体只能用于创建它的 World。
- 销毁后旧 ID 失效，即使底层槽位被复用也不会指向新实体。
- 实体 ID 不能持久化为场景对象身份。

`world::entity_is_alive` 对已销毁或其他 World 的实体返回成功并将结果设为 0；需要执行销毁、
读写组件等操作时，这类实体返回 `result::invalid_handle`。


## ECS 与 System

EnTT `3.15.0` 只用于 World 的内部组件存储。Gneiss 公共头、Entity ID 和 C ABI 不依赖 EnTT。
内部 System Scheduler 按注册顺序串行执行，遇到第一个失败结果立即停止并保留该结果。

Camera 与 Mesh Renderer 是当前原生组件，其行为由
[Render 资源、组件与帧提取](render.md)定义。Transform 和 Camera 可通过
[Type Registry 与反射元数据](reflection.md)注册并按 World、Entity、Type GUID 和 Field ID 访问；
公共接口不暴露 EnTT 或组件地址。可持久化对象 UUID 仍由场景格式负责，不使用运行时实体 ID。
