<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景实例化关联回执迁移

## 范围

推进 [PLAN-105](../plans/PLAN-105-game-object-runtime-model.md) 的文档投影边界。
instantiate_prepared、replace_prepared 及 Prefab 创建返回本次创建的对象与来源关联。
Runtime 场景加载回执交付关联，编辑器接管时验证实体有效性及成员数。
scene_projection 维护自身映射，普通增删、Prefab 替换及完整重建显式更新关联；
删除 refresh 扫描 get_node_info().prefab 的反向推导。

## 验证

Windows Clang Debug 默认构建及 84 项相关 CPU 回归通过。
新增同源不同实例映射、重复地址/实体拒绝、失败不覆盖旧映射及实例化回执身份检查。
完成改动行格式整理及 git diff --check；未运行 GPU 大场景或远端 CI。

## 限制

运行层 prefab_binding、reference_ 与 Prefab 专用结构更新仍存在。
持久化父关系尚未全部改用对象地址；本记录不代表 PLAN-105 完成。
