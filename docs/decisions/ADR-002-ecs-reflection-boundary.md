<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-002：ECS、反射与序列化边界

- 状态：已接受
- 日期：2026-08-26

## 背景

Gneiss 需要在 `0.1.0` 建立 World 和 ECS，后续还需要支持编辑器属性面板、场景序列化、Undo/Redo、
Prefab 和数据迁移。若自研完整 ECS，首版需要同时解决实体回收、组件存储、查询和调度；若把 EnTT
类型、Meta 或 Snapshot 直接作为项目契约，持久化数据和编辑器又会与第三方实现绑定。

## 决策

- 使用 EnTT `3.15.0` 作为内部 ECS 存储和查询实现，以 Git submodule 锁定版本。
- Gneiss 定义自己的 `entity_id`、World 和 System 边界；公共头文件不包含 EnTT，公共 ABI 不出现
  `entt::entity`、类型哈希或 Registry。
- EnTT Meta 可以作为未来反射实现的内部工具，但不作为公共反射 API 或持久化 Schema。
- Gneiss 后续建立独立 Type Registry，使用稳定类型 UUID、字段 ID、版本和迁移函数驱动编辑器与
  序列化。
- 场景文件保存稳定对象 UUID，不保存运行时 entity 值。加载场景时建立对象 UUID 到 entity ID 的
  临时映射。
- EnTT Snapshot 只用于运行时快照、World 复制或网络同步候选，不直接作为长期场景文件格式。
- System 由 Gneiss 显式注册并按确定顺序串行执行；EnTT Organizer 和并行调度不进入首版。

## 影响

- `0.1.0` 可以复用成熟 ECS，集中验证 Scene Tree、Service 和渲染闭环。
- 编辑器、反射、序列化和版本迁移不会因未来替换 ECS 实现而失去数据契约。
- Gneiss 必须维护一层薄 World 适配，避免 EnTT API 扩散到模块边界。
- 自定义组件仍需要显式注册反射与序列化信息；引入 EnTT 不会自动解决长期数据兼容问题。

## 替代方案

- **自研最小 ECS**：控制力最高，但会显著扩大首版范围，且编辑器 Schema 仍需单独实现。
- **直接公开 EnTT**：开发最快，但会把公共 API、场景数据和插件生态绑定到第三方版本，拒绝采用。
- **同时维护两套 ECS 原型**：验证充分但产生一次性代码；当前需求已足以支持 EnTT 适配方案。
