<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-109：Runtime 属性写入协议实施记录

## 结论

M-109 已完成。Editor 与 Runtime 的私有 IPC 协议次版本提升为 2，并建立属性写入命令、执行结果和
`runtime_property_edit_v1` 能力协商；本阶段只定义传输契约，尚未让真实会话宣告或执行该能力。

## 已实现内容

- `property_write` 携带会话 ID、命令 ID、运行时对象 ID、generation、Type ID、Field ID、期望修订号
  和类型化值。
- `property_write_result` 携带稳定结果码、诊断消息、当前修订号，并在成功时携带 Runtime 规范值。
- IPC 属性值拥有字符串与数组内存，不保留公共属性容器中的借用指针。
- 支持 Bool、Int64、UInt64、Float32、Float64、String、Type ID、Vec3 和 Quaternion。
- Type ID 使用固定 32 位十六进制文本；非有限浮点、无效 ID、零命令、零修订和超长文本会被拒绝。
- 专用消息由独立编解码器处理，通用控制消息解码器保持返回 `unsupported` 的既有边界。

## 验证结果

- 属性协议专项测试覆盖九种值往返、成功与拒绝结果、无效命令、非有限浮点、损坏负载和能力协商。
- Windows Clang Shared Debug 全量 104/104 通过。
- 公共 C ABI 未变化；新增类型仅存在于私有 C++ IPC 层。

## 后续边界

M-110 才会建立 Runtime I/O→主线程命令队列、修订管理和真实属性执行。在该闭环完成前，Runtime
不得宣告 `runtime_property_edit_v1` 能力。
