<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-127：表示转换作为静态类型能力

状态：已接受（2026-11-18），取代 ADR-126 的独立适配器表与上下文覆盖决策。

类型已携带由 C++ T 生成的操作表，因此固定表示转换无需额外 type_id 查找表，也不必登记
类型扩展。Meta 增加独立 representation_ops，与结构类别、value_ops 并存。
traits 只描述纯类型转换，不接收序列化 context/reader/writer；格式遍历、诊断路径、资源
解析策略仍归 serialization。普通整数/字符串沿用基础后端操作。

通过回调借用栈上的表示值和候选值，不增加对象所有权层。特化需在首次实例化前可见，遵守
ODR；放弃运行时按上下文替换同类型表示。字段、元数据和按文件 GUID 恢复多态类型仍需注册。
