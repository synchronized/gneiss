<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-105：序列化格式边界

状态：已接受，实施中。日期：2026-11-05。

## 决策

Meta 负责类型与属性访问；序列化放入 `engine/core/serialization`。
JSON 入口显式位于 `serialization::json`，文本输出与字节输出分别命名。
资源专用二进制 codec 仍负责自身格式，不由 JSON 入口猜测或选择。
当前注册表的字节 codec 在嵌套值中实际要求 JSON，应明确标注该限制。

后续抽取 reader/writer，使字段、容器与引用遍历不依赖 yyjson。
JSON 节点适配限定在专用接口；不增加第二套反射注册表或通用 DOM。
第二种格式有真实使用需求后再实现，不预先增加 MessagePack 依赖。

## 影响

内部调用显式选择格式，不保留含糊的 Meta 序列化别名。
迁移保持文件布局、诊断及对象引用语义；格式专用回调仍可保留。
不得把接口搬迁描述为已经完成格式无关遍历。

序列化基础设施可以内部取得默认注册表；业务调用继续使用无 registry 重载。
边界检查同时覆盖 Meta 和 Serialization，场景描述值仍不得包含这些执行接口。
