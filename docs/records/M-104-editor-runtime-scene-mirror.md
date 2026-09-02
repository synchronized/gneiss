<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-104：Editor Runtime 镜像与层级视图实施记录

## 结论

M-104 已完成 Runtime 场景快照的端到端传输和 Editor 只读镜像。Editor 与 Runtime 现会协商
`runtime_inspection_v1`；Runtime 在主线程定期采样场景，Editor 将完整快照或增量原子应用到独立镜像，
并在 Scene Hierarchy 中显示明确标记为只读的 Runtime 层级。

## 已实现行为

- `inspection_snapshot` 使用协议 1.1 的有界 JSON 负载，包含会话、序号、完整标记和 upsert/remove。
- 节点负载包含对象 ID、generation、父级、UUID、名称、局部 Transform 和组件标志。
- Runtime 每 100 ms 在 Application 更新回调的主线程安全点采样；I/O 线程只传输已拥有的数据。
- Editor 镜像按会话和序号应用消息，忽略重复批次；缺口、旧会话或非法图会进入等待完整快照状态。
- 完整快照原子替换镜像，增量在父级、generation、UUID 唯一性和无环验证后提交。
- 每次 Runtime 启动都会清空旧镜像；作者 Scene Session 不会被运行态数据修改。
- Scene Hierarchy 同时显示 `Runtime (Read-only)` 和 `Author Scene`，运行态节点不提供编辑操作。

## 验证

- IPC 测试覆盖快照 JSON 往返、中文名称、Transform、组件标志和删除操作。
- Editor 镜像测试覆盖非 1 起始完整快照、增量、重复、序号缺口、跨会话重同步及非法父级原子失败。
- 真实 `runtime_process` 测试验证启动后收到非空 Runtime 场景镜像，并继续通过暂停、恢复和停止流程。
- Windows Clang Shared Debug 完整构建及 103 项测试通过。

## 后续边界

M-105 将在只读镜像上增加组件和属性检查。M-107 再补充显式重同步请求、分批大快照、队列水位与
过载恢复；当前单个快照仍受 64 KiB IPC JSON 上限约束。
