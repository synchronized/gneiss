<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 属性范围查询收敛

## 变更

type 和 const_instance 的 properties 直接返回 property_range；未注册、无效或非对象类型
返回空范围。删除未使用的注册表 properties 入口和重复查询实现。
范围和迭代器继续保活注册表，不持有目标对象。
序列化、快照、Prefab 覆盖和属性编辑在操作边界检查 registered，避免将未注册对象当成空对象保存。

## 验证

编辑器及相关目标重新编译通过，12 项定向 CPU 测试全部通过。
覆盖无效/标量类型空范围、未注册对象拒绝序列化、迭代器保活、嵌套 JSON、场景描述、
组件反射与编辑、材质编辑和运行时属性编辑。
日志：build/plan108-property-range-build.log、build/plan108-property-range-tests.log。
git diff --check 通过；未运行 GPU、大场景和远端操作。
本批不代表 PLAN-108 阶段 4a–4c 或整体计划完成。
