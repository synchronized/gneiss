<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-035：分离 IPC 传输与应用协议

- 状态：已接受
- 日期：2026-09-04

## 背景

最初的 `gneiss_uv_runtime` 同时包含 libuv 运行循环、信封传输、通用分发，以及 Editor 与 Runtime
专用的 Session、Control、Inspection、Statistics 和 Property 协议。应用语义因此进入通用 I/O
目标，任何协议扩展都会使底层传输重新依赖场景、反射和编辑器协作模型。

## 决策

- `src/io` 只保留 libuv 运行支持；`src/ipc` 通过独立目标 `gneiss_ipc` 提供信封、Dispatcher 和
  Transport 等通用 IPC 基础设施。
- Editor 与 Runtime 共用的协议实现放入 `apps/common/ipc`，由独立目标
  `gneiss_app_ipc_protocol` 提供。
- Operation Router 与标准域组合属于应用协议层；它依赖已知协议域，不作为通用 I/O 能力。
- Runtime 和 Editor 依赖应用协议目标；应用协议目标单向依赖 `gneiss_ipc` 和 Engine 数据类型，
  `gneiss_ipc` 再依赖 `gneiss_uv_runtime`，不允许底层目标反向依赖应用协议。
- 两层均为内部 C++20 目标，不进入公共 C11 SDK，也不承诺独立 ABI 稳定性。

## 影响

- 新增业务域、操作或负载格式不会扩大 libuv 与 Transport 的职责。
- 独立工具可以复用信封和 Transport，而无需链接场景检查与属性编辑协议。
- Editor、Runtime 及相关测试通过 `gneiss_app_ipc_protocol` 获得协议头文件和实现。
- 本次迁移不改变线格式、协议版本、消息方向和线程模型。

## 替代方案

- 继续将所有协议放在 `src/io`：目录较少，但应用语义与底层 I/O 的依赖方向错误。
- 合并进 `gneiss_app_project`：双方确实共用，但工程描述与进程协议生命周期不同，会形成新的宽泛
  公共目标。
- 立即拆成更多独立域库：边界最细，但现有规模不足以抵消目标数量和构建组合成本。
