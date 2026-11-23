<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 属性局部注册

## 实现

property/readonly 返回局部代理，支持 property(...)(extensions...)；代理保存注册器和字段索引。
原有不附加扩展的连续属性注册保持可用，类型 metadata/extend 仍作用于类型。
所有生产与测试调用迁移，property_metadata 旧入口删除；仍要求显式 commit。
注册器移动/销毁后代理失效；不引入代理拥有注册器或隐式提交。

## 验证

编辑器及相关目标编译通过，type_builder、serialization、serialization-archive、
reflection-json、scene_description 共 5 项 CPU 测试通过。
新增验证覆盖字段扩容后的代理索引及重复字段扩展拒绝；既有测试覆盖类型扩展隔离、
getter/setter、字段保存策略和引用策略。构建日志：build/plan108-property-builder-build.log。
git diff --check 通过。未运行 GPU、大场景及远端验证。
