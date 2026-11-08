<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-110：统一 Game Object 存储与 Prefab 文档边界

## 状态

2026-11-07：已接受，实施见 [PLAN-105](../plans/PLAN-105-game-object-runtime-model.md)。
本决策调整 ADR-109 中组件身份由 component_entry 外壳保存的选择，并将场景树/实体双存储
收敛为同一实体上的运行层级；既有实现不会因决策接受自动视为已迁移。

## 决策与原因

component_description_base 保存组件 UUID，类型 GUID 继续表达类型。
组件领域基类本来就具有实例语义，外置包装造成重复访问和身份传递；不向 meta::object 注入场景身份。
共享描述保持只读，编辑/复制创建独立候选，不能原地修改共享 UUID。

game_object_description 通过 optional prefab 表达来源；Prefab 存在时独立 components 必须为空。
相较 scene_entry variant，通用身份、名称、层级和变换操作更直接，代价是领域校验负责互斥约束。

Prefab 来源和覆盖归文档，组合后产生完整有效对象。运行对象不记录覆盖或在赋值时生成覆盖。
编辑器维护来源地址到实体映射；运行时实例化和编辑器保存不共用一套可变记录。

World 唯一拥有实体、组件和场景成员；game_object/scene_instance 为非拥有访问句柄。
层级是同一实体的数据，删除独立节点到实体的第二套生命周期。
运行句柄校验 generation 与 World 归属，描述 UUID 由场景实例域限定。
不让 game_object 继承 meta::object 或使用强引用延长已删除实体的寿命。

## 影响与替代方案

保留节点与 ECS 双存储可以隔离树遍历，但增加身份同步、删除和失效边界。
全部改为 refcount 对象更接近传统对象树，却与现有 ECS 所有权重叠；本次不选择。
采用统一实体存储需要迁移层级、渲染采集、场景卸载和编辑器关联，不能只更名后宣称完成。
原决策允许文档历史保留前后不可变版本及完整重建。2026-11-08 此项由
[ADR-111](ADR-111-document-history-projection.md) 调整为局部变更历史与统一增量投影。
