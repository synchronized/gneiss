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

## 2026-11-17：Window 与 Input 领域

窗口配置及窗口服务使用 gneiss::window，窗口实现依赖 Granit，归 Platform，移出 Core。
输入原始事件、键码和设备状态使用 gneiss::input，保留 Core 基础定义；动作 ID/状态与输入服务
归 Function，动作映射资产归 Resource。目录表达层级，命名空间表达同一个输入领域。
Application 协调窗口采集和输入消费；Window 不依赖 input::service，Input 不依赖窗口实现。
显式反射 GUID 和持久化名称保持不变；不保留旧类型别名，不新增独立库或后端接口。
