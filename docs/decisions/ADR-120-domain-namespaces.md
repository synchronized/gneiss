<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# ADR-120：目录表达分层，命名空间表达领域

2026-11-11，已接受。

统一 gneiss::engine 无法进一步区分职责，实际领域由 meta、math 等表达。
移除 engine 命名空间层：领域迁至 gneiss::meta/math/events/serialization/logging/files，任务迁至 gneiss::tasks。
其余通用值及已有明确命名的服务直接使用 gneiss；host/editor/runtime 保持原有命名空间。
目录结构和依赖方向保持不变，继续通过 target 接口和边界检查维护分层。
不保留 engine 兼容别名；不改变显式 GUID，编译器生成的类型名允许随 C++ 名称变化。
私有声明优先在所属领域 detail 内，禁止借此引入跨模块依赖。
代价为源码/ABI 名称变化，内部消费者需整体重编译。详见 [PLAN-117](../plans/PLAN-117-domain-namespaces.md)。
