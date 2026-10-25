<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-051：C++ 类型注册简化

## 状态

实现完成；Windows Clang Debug 全部启用目标编译通过，格式、Core 边界、公共接口和 diff 检查通过。
新增用例已编译，未执行测试或 CI，运行验收按用户要求延后。当前接口见[反射参考](../reference/reflection.md)。

## 目标与决策

通过 type_identity 提取静态名称和 GUID；成员指针及有类型访问器生成现有 property_binding。
集中登记类型及全部绑定，失败不留下不完整类型。保留字段身份和现有校验。
不引入对象基类、方法系统、任意对象 value 或资源事务。

## 实施与验收

1. 静态身份提取及冲突校验。
2. 薄 builder 和 Registry 集中登记。
3. 迁移 Transform、Camera，维持只读字段与写入失败语义。
4. 补充身份、重复字段、失败不登记及属性读写测试，编译全部启用目标。

## 未决问题

无；行为测试留待架构统一验收。
