<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# World 与 Entity

## World 生命周期

`gneiss_world_create` 创建空 World，`gneiss_world_destroy` 销毁 World 及其全部实体和组件。World
句柄是不透明的 64 位值，零值无效，重复销毁返回 `GNEISS_ERROR_INVALID_HANDLE`。

World 及其实体只能在创建 World 的线程访问。跨线程调用返回 `GNEISS_ERROR_INVALID_STATE`。当前
线程约束保证 EnTT Registry、组件与 System 更新具有确定性；并行 System 需要独立设计和性能证据。

`gneiss_world_desc` 使用 `struct_size` 支持后续追加字段。调用方应使用
`GNEISS_WORLD_DESC_INIT` 初始化，不得设置保留字段。

## Entity 生命周期

`gneiss_world_entity_create` 返回运行时 `gneiss_entity_id`。实体 ID 包含 World domain 和 EnTT
实体 generation，但编码布局不是公共 ABI：

- 零值表示无效实体。
- 实体只能用于创建它的 World。
- 销毁后旧 ID 失效，即使底层槽位被复用也不会指向新实体。
- 实体 ID 不能持久化为场景对象身份。

`gneiss_world_entity_is_alive` 对已销毁或其他 World 的实体返回成功并将结果设为 0；需要执行销毁、
读写组件等操作时，这类实体返回 `GNEISS_ERROR_INVALID_HANDLE`。

## C++ 包装

`gneiss::world` 独占 World，默认不可复制但可以移动，析构时自动销毁。`gneiss::entity_id` 只包装
运行时标识，不拥有实体；World 销毁后所有关联实体 ID 都失效。

## ECS 与 System

EnTT `3.15.0` 只用于 World 的内部组件存储。Gneiss 公共头、Entity ID 和 C ABI 不依赖 EnTT。
内部 System Scheduler 按注册顺序串行执行，遇到第一个失败结果立即停止并保留该结果。

Camera 与 Mesh Renderer 是当前首批公共组件，其行为由
[Render 资源、组件与帧提取](render.md)定义。通用组件注册、反射 Schema 和可持久化对象 UUID
尚未落地，不属于当前公共能力。
