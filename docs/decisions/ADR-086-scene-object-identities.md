<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-086：场景对象身份与资源 RID 分离

状态：已接受。

## 决策

原生 World 访问不再经由全局 RID 注册表。场景域拥有 World，反射和组件访问借用 world&。
World 的所属线程、实体有效性检查保留。借用者必须先于拥有者销毁。

场景实例和节点保留非拥有、可校验失效的身份，但使用独立的强类型 ID。
Prefab 刷新 token 表示操作记录，使用独立类型。不得将它们与渲染资源 RID 混用。

## 理由与影响

C ABI 移除后，全局 World 注册表造成对象引用和句柄同时传递、反射查回同一对象的冗余。
直接引用减少中转，但借用生命周期改由明确的所有权关系保证。
节点删除、场景卸载后仍需拒绝旧标识，因此保留代次和所属域校验，不能全部替换为裸指针。
不为本次迁移重新建立公共对象 SDK。

实施见 [PLAN-053](../plans/PLAN-053-scene-object-identities.md)。
