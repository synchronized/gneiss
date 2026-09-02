<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-113：Runtime Transform 作者回写实施记录

## 结论

M-113 已完成。用户可以把选中 Runtime 节点当前的局部 Transform 显式应用到作者场景；运行时编辑
仍默认只属于当前会话，不会自动污染作者数据。

## 已实现内容

- Runtime Inspector 提供“应用 Transform 到作者场景”按钮，只对具有非空 UUID 且能映射到当前作者
  场景节点的 Runtime 对象启用。
- 回写保存稳定 UUID、应用前值与 Runtime 当前值，不保存 Runtime 对象 ID、实体 ID 或原生指针。
- 整组局部 Transform 作为一条既有 Editor 命令执行，支持撤销和重做。
- 成功回写进入现有命令历史并更新场景脏状态，后续保存继续使用既有原子场景保存路径。
- 无 UUID、运行时生成节点或已经从作者场景删除的节点不会被创建或猜测映射，操作明确拒绝。
- 本阶段不回写 Camera、Mesh Renderer、运行时新增节点或整个 Runtime 场景。

## 验证结果

- Runtime Launch 测试覆盖 UUID 映射、Transform 回写、脏状态、保存点、撤销和重做。
- 测试覆盖不存在作者 UUID 的 Runtime 节点并确认返回 `not found`。
- Windows Clang Shared Debug 全量构建通过，CTest 106/106 通过。
- 公共 C ABI 与 IPC 协议均未变化；回写能力只属于 Editor 作者工作流。

## 后续边界

M-114 将强化修订冲突、响应乱序、队列过载、慢 Runtime、崩溃与连续 Play 的恢复行为，并验证控制
消息不会被属性流量阻塞。
