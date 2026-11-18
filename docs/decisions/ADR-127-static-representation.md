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

能力访问通过 type::capability<representation_capability>() 返回的类型能力对象提供，避免在 type 上累积
具体读写操作。序列固定长度、身份能力等无实例查询同样由 sequence_capability 承担；
容器实例的元素访问仍由 sequence_view 承担。两者共享静态 ops，不复制或维护能力状态。

统一命名为 capability（类型能力）与 view（绑定实例）。容器能力经 bind 返回 view，
表示能力直接提供 encode/decode。保留静态 ops 来源和 instance 的编译期可写性；
不引入独立能力注册表或上下文覆盖。实例的 as_* 便捷方法共用 capability 绑定规则。

元素类型查询属于对应容器 capability（variant 不适用）；枚举项查询属于 enum_capability。
未注册原生枚举具有枚举能力但没有注册条目。type 只保留通用身份、类别、注册数据查询入口。
