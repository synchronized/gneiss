<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-103：Runtime 场景快照与增量生成实施记录

## 结论

M-103 已完成 Runtime 主线程侧的场景检查快照生成器。生成器可从场景实例查询结果构造完整快照，
并将后续节点创建、删除、重命名、父级、Transform 和组件集合变化折叠为确定性增量。它只生成拥有
数据，不执行 IPC 或访问 Editor 状态。

## 已实现行为

- 首次采样和显式重同步生成完整快照，后续无变化采样返回 `not_ready`，不消耗消息序号。
- 每个输出批次携带 M-102 定义的会话 ID 和严格递增序号。
- UUID 在会话内维持对象身份；销毁后槽位可复用，但 generation 必须递增。
- 父节点由 Runtime 原生节点映射为检查对象 ID，不向传输层暴露 Scene Node 句柄。
- 删除操作先于同批新增或更新操作生成，节点更新保持场景作者顺序。
- 节点数、变化数和字符串长度均有明确上限；非法父级、重复 UUID 或重复原生节点会被拒绝。
- `capture_scene` 通过现有 C API 在调用线程读取场景实例，libuv I/O 线程无需接触 Scene Tree 或 ECS。

## 验证

- 单元测试覆盖完整快照、无变化、重命名、Transform 更新、删除、槽位复用、generation 递增、父子
  映射、强制重同步、非法会话、重复原生节点、孤儿父级、重置和空场景稳定性。
- Windows Clang Shared Debug 目标构建与 `gneiss.runtime.scene-inspection` 测试通过。

## 后续边界

M-104 将定义快照 IPC 负载、Runtime 主循环采样节奏和 Editor 只读镜像。生产握手仍不宣告
`runtime_inspection_v1`，直至 Editor 能完成完整快照与增量应用。
