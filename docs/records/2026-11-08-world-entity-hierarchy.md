<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# World 层级实体存储迁移

## 范围与结果

推进 [PLAN-105](../plans/PLAN-105-game-object-runtime-model.md) 的运行存储部分。
删除 function/scene/scene_tree 的独立节点槽位和 generation；层级、身份和局部变换改为
World 实体组件，function/world/world_hierarchy 只借用该存储。
节点查实体与实体查节点不再扫描列表，删除任一入口都删除对象子树的实体及组件。
修复重复挂到同一父节点会丢失父节点 children 登记的问题。

增加 game_object 借用接口并接入场景创建；只保存 World 指针和实体 ID，
不缓存可移动组件地址。世界变换沿用即时计算，无新增缓存或脏标记协议。
当前行为见 [World](../reference/world.md) 与[层级](../reference/scene-tree.md)。

## 验证

- Windows Clang Debug 默认构建通过。
- 84 项相关 Scene、Prefab、Editor、World、组件、渲染采集和边界测试通过。
- 新增空句柄、跨 World、跨线程、重复挂接、循环关系、纯 ECS 实体、generation 失效、
  子树销毁及 EnTT 组件紧凑存储移动检查；线程检查补充后单独复跑原生生命周期测试通过。
- git diff --check 通过；未运行 GPU 大场景、完整远端 CI 或发布。

## 尚未完成

scene_instance 仍拥有成员记录；Prefab 来源绑定与旧 node_* 调用方仍在迁移。
本记录不代表 PLAN-105 已完成，不承诺跨场景重新挂接或 World 销毁后继续使用借用句柄。
