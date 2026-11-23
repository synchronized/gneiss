<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 只读与可写实例统一

## 实现

instance/const_instance 改为 basic_instance<true/false>，共用一个类型句柄和访问位置。
可写实例不再嵌套只读实例及重复指针；五类容器/包装视图复用同一表示。
写操作通过 C++20 requires 限制，as_const 借用同一地址；getter/setter、受约束属性整体替换、
继承地址调整与不保活业务对象的规则保持。无效实例的 as_variant 与其他视图统一返回 invalid_state。

阶段 4a 的类型能力、直接查询及类型比较和阶段 4b 的动态类型/实例布局已经完成；
属性局部 builder 和后续序列化上下文仍待实施。

## 验证

编辑器与相关 CPU 目标编译通过，9 项测试通过：实例、容器、分类 Ops、序列化、Archive、
反射 JSON、材质编辑、作者属性编辑、组件编辑。
新增静态断言覆盖只读禁止赋值、两种特化的别名与相同大小；既有测试覆盖 setter、继承和引用。
构建日志：build/plan108-basic-instance-consumer-build.log。
git diff --check 通过。未执行 GPU、大场景或远端操作。
