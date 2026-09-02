<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-116 Prefab 格式、身份与组合 Spike 记录

## 结论

M-116 已验证独立 `gneiss.prefab` v1 envelope、单根节点约束和复合作者身份。Prefab 节点编码复用
现有场景节点解析与校验，不建立第二套组件 Schema；实例根作为 Prefab 源根的父节点，继续沿用
Scene Tree 的父子 TRS 组合顺序。

## 已确定边界

- Prefab 作者文件使用 `format`、`version`、`prefab_uuid` 和 `objects` 顶层字段。
- `objects` 复用场景 v2 的节点、Transform 和组件编码，并且必须恰有一个无父级根节点。
- 节点作者身份由 `instance_uuid` 与 `source_node_uuid` 组成，同一源节点在不同实例中不会冲突。
- 复合身份当前属于 Engine 私有描述类型，不作为 RID，也不在 Spike 阶段冻结公共 ABI。
- 解析失败不发布部分结果，并通过与场景描述一致的结果码、字段路径和中文诊断返回。

## 验证结果

| 验证 | 结果 |
| --- | --- |
| Prefab v1 与两节点单根树解析 | 通过 |
| 未来版本拒绝与诊断路径 | 通过 |
| 多根 Prefab 拒绝 | 通过 |
| 同源节点的不同实例复合身份区分 | 通过 |
| 实例根与源根 Transform 父子组合参考值 | 通过 |
| Windows Clang Debug 场景与 Prefab 描述专项测试 | 2/2 通过 |

正式 VFS Loader、规范 URI、依赖收集、缓存及未知字段往返由 M-117 完成。
