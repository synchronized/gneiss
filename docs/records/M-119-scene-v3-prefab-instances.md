<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-119 场景格式 v3 与 Prefab 实例声明实施记录

## 结论

M-119 已将引用型 Prefab 纳入场景作者格式和 Scene Instance 所有权。场景 v3 保存紧凑实例声明，
加载时创建独立 Runtime 投影，保存时只回写实例根 Transform，不展开 Prefab 源节点。

## 格式与迁移

- v3 新增必需的 `prefab_instances` 数组；旧 v1/v2 场景迁移后得到空数组，原有语义不变。
- 每项保存 `instance_uuid`、`prefab` URI、普通对象父级、可选名称与实例根 Transform。
- 普通对象 UUID 与实例 UUID 共用唯一性空间；实例父级不能指向另一个 Prefab 实例。
- 当前版本未知字段继续保留；迁移发现已有目标字段时拒绝覆盖。

## Runtime 所有权

- Scene Instance 在普通对象提交后加载 Prefab，并把实例根挂到声明的普通对象父级。
- 任一 Prefab 或依赖缺失时，已创建的普通对象和先前实例均回滚，场景句柄不发布。
- 卸载场景时先释放 Prefab Runtime 投影，再销毁普通对象，旧 Runtime ID 随即失效。
- 当前公共节点枚举仍只覆盖普通 `objects`；Prefab 层级呈现属于 M-120。

## 验证结果

| 验证 | 结果 |
| --- | --- |
| v1→v2→v3 与 v2→v3 迁移 | 通过 |
| v3 实例声明解析、URI 与身份校验 | 通过 |
| 实例 UUID 冲突与实例嵌套父级拒绝 | 通过 |
| 未知字段序列化往返 | 通过 |
| 场景加载、Prefab Runtime 投影与卸载 | 通过 |
| 缺失 Prefab 的整场景原子回滚 | 通过 |
| 保存结果不含源节点或 Runtime ID | 通过 |
| Windows Clang Debug 完整回归 | 109/109 通过 |
