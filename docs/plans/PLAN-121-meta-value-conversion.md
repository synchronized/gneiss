<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-121：类型结构与整体值转换解耦

状态：已完成本批实施与本地验收。

## 目标与边界

结构类别回答如何遍历类型，value_ops 回答能否整体读写为 value。二者独立，避免将有字段的
对象仅因可放入 value 就改成标量。采用 [ADR-125](../decisions/ADR-125-meta-value-conversion.md)。

## 实施与验收

- [x] scalar_ops 只保留标量类别，type_ops 独立保存 value_ops。
- [x] 使用 value_traits<T> 适配整体转换，标量自动适配。
- [x] instance 的整体读写接入；对象属性 setter 通过候选替换执行，保持业务拒绝语义。
- [x] 验证对象转换、错误输入不修改目标、字段 JSON 格式不变及现有实例回归。
- [x] 更新接口说明并完成本地提交。

## 使用流程

在首次查询类型能力前定义 value_traits<T>；注册字段后冻结 registry。使用 meta::view 借用
调用方持有的对象，read_value() 取得独立值，assign(value) 执行类型适配；输入错误返回 error。
借用不持有对象，销毁 registry 和对象前结束借用。转换不会触发注册或改变序列化类别。

## 后续独立事项

本批不增加 aabb3 的 value 分支，不增加通用二进制后端。aabb3 的原生 value 分支、类型身份、
字段注册和持久化格式应作为独立完整链路接入；不能将本次能力拆分描述为已支持该类型。

## 本地验收（2026-11-18）

Clang Debug 构建通过；meta-type-ops、meta-instance、reflection-json、property_access、
meta-access-contract、serialization-archive、value_edit 共 7 项回归通过。
未执行 MSVC、静态库和跨平台矩阵；不代表完整版本验收。新增测试使用真实实例、注册和 JSON
入口，覆盖对象转换、错误输入不修改目标以及 setter 拒绝后的原值保持。
