<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-102：Runtime 检查会话与对象标识实施记录

## 结论

M-102 已完成检查协议的身份和顺序基础。IPC 协议次版本提升为 1，并定义
`runtime_inspection_v1` 能力名、会话内对象 ID、generation、检查消息标记和顺序跟踪器。Runtime
尚未宣告该能力，待 M-103 与 M-104 形成快照收发闭环后再启用协商。

## 已实现行为

- 运行态对象以非零 64 位对象值和非零 32 位 generation 组成，仅在所属 Runtime 会话内解释。
- 检查消息使用非零会话 ID 和从 1 开始的 64 位单调序号。
- Editor 侧顺序跟踪器区分接受、重复、缺口、旧会话和非法标记。
- 重复、缺口和旧会话消息不会推进期望序号；重置后旧消息失效。
- 序号耗尽时跟踪器自动失效，避免无声回绕后接受旧数据。
- 能力协商测试已覆盖 `runtime_inspection_v1`，但生产会话仍只声明已经可用的控制、心跳与日志能力。

## 验证

- Windows Clang Shared Debug 完整构建通过。
- Windows Clang Shared Debug 完整 101 项测试通过。
- IPC 协议测试覆盖零值对象、generation 区分、非法会话、重复、缺口、旧会话、连续消息和重置。
- Runtime IPC Session 与 Editor Runtime Process 回归通过。

## 后续边界

M-103 负责生成带上述标记的完整场景快照与增量；M-104 建立 Editor 只读镜像。只有收发闭环可用后，
双方才应在真实握手中宣告 `runtime_inspection_v1`。
