<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-079：内部命名空间按架构层组织

- 状态：已接受。
- 日期：2026-10-24。

## 决策

内部业务类型统一位于 gneiss::engine、gneiss::runtime、gneiss::editor、
gneiss::tooling、gneiss::host。目录不再逐级映射为命名空间。
单文件实现使用匿名命名空间，头文件实现细节可以使用 detail。
公共 C 接口与 gneiss 下的公共共享值类型不变；不提供旧命名兼容别名。

Meta 是上述平铺规则的明确例外：反射描述、注册、查询和编解码接口位于
`gneiss::engine::meta`，避免通用名称与业务模块冲突。基础值类型和 traits 定制点仍在
`gneiss::engine`；不为旧 Meta 限定名保留兼容别名。

## 影响

限定名缩短，重复类型别名可删除。真正同名但不同职责的对象需明确命名。
目录、目标可见性和包含依赖检查继续维护分层；不能以命名空间代替依赖约束。
内部 C++ 消费者需统一重编译，不改变对象所有权和运行时行为。

## 替代方案

保持每个子目录对应一个命名空间，隔离名称容易，但限定名过长并重复表达目录职责。
