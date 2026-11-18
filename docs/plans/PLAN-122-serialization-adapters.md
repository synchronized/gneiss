<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-122：格式无关的值类型适配器

状态：已完成本地实施与验收（2026-11-18）。

## 已确认目标

Meta 保留内存类型与 value 转换；serialization::adapter<T> 将类型转换成可遍历的表示类型。
GUID 使用 optional<string>，数学类型使用固定 float 数组。JSON 后端不识别这些业务类型。
保留现有 GUID nil/null、标准 UUID 字符串、数学分量顺序和 value 的 $kind/$content 格式。
不新增通用二进制后端，不把适配器自动绑定到 value_ops。

## API、所有权与错误

用户特化 adapter<T> 的 representation_type、encode(const write_context&, const T&) 和
 decode(const read_context&, const representation_type&)；返回 result，失败保持目标不变。
register_adapter<T>(adapter_registry&) 建立类型擦除入口。上下文借用配置完成的适配器表；
表必须覆盖操作生命周期，操作期间不可修改。未配置自定义表时使用内置表。
通过反射实例处理普通属性、setter 属性和 value 分支，setter 仍负责最终提交。
配置重复注册返回错误；表示递归受上下文深度上限约束。

## 实施与验收

- [x] 实现独立适配器表、上下文选择和格式无关的递归入口。
- [x] 接入 GUID 与自定义类型，覆盖根值、字段、错误输入与 setter。
- [x] 接入 vector/quaternion/matrix，移除 JSON 内特例，保持固定长度与顺序。
- [x] 验证 value 分支及现有序列化消费者，更新 Reference 与本地验收记录。

更具体的边界见 [ADR-126](../decisions/ADR-126-serialization-adapters.md)。

## 实施补充

发现组件自定义 codec 直接调用后端 scalar 处理 GUID；补齐 write_node 与带 node 的
read_into/decode 入口，并迁移该消费者。适配器也复用这些入口，不保留专用的表示递归桥。
新增用例覆盖自定义类型、setter 拒绝、表示递归环、多态标量内容及非空引用/null 歧义。

## 验收结果

- Clang Debug：meta-type-ops、reflection-json、serialization-archive、serialization、
  scene_description、value_edit、component-description 共 7 项通过。
- MSVC Release：meta-type-ops、reflection-json、serialization-archive、serialization、
  component-description 共 5 项通过。
- 改动行格式检查与 git diff --check 通过。未执行静态库及跨平台矩阵，不代表版本发布验收。
- 原有 JSON 数据约定保留；对象/属性校验继续执行。普通读取仍不提供整份文档的事务回滚。
- 表示转换计入递归预算；设置极小 maximum_depth 的调用者需要为表示容器留出层级。
