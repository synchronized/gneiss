<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-128 Runtime 属性回写到 Prefab 覆盖实施记录

## 结果

M-128 已让 Runtime 检查快照携带 Prefab 复合作者身份，并将显式 Transform 回写分流到普通场景
节点、Prefab 实例根或指定实例中的来源节点。来源节点的回写通过 Scene 作者接口形成实例局部覆盖，
不会修改共享 Prefab 资产。

## 已验证行为

- Runtime 检查快照包含 Prefab 实例根和来源投影，不再只包含普通作者节点。
- Prefab 来源节点使用“实例 UUID + 来源节点 UUID”标识；同一来源的多个实例拥有独立 Runtime ID。
- Editor 回写命令按复合身份重新定位目标，Undo/Redo 不依赖可能过期的 Runtime ID。
- Prefab 实例根按实例 UUID 写回，来源节点按完整复合身份写回；普通节点继续按场景 UUID 写回。
- 不完整的复合身份返回 `invalid_argument`，刷新后已不存在的目标返回 `not_found`。
- 检查协议次版本提升到 1.3，并以 `runtime_inspection_v2` 能力协商新增作者身份字段。

## 验证

Windows Clang Debug 的 Runtime、Editor 及相关目标构建通过。协议往返、IPC 会话、Runtime 场景检查、
Runtime 属性写入和 Editor Session 测试通过；测试覆盖真实 Prefab 场景采集、多实例复合身份、回写
覆盖以及 Undo/Redo。

Lantern Gallery 的差异化实例与完整跨平台验收由 M-129 接续完成。
