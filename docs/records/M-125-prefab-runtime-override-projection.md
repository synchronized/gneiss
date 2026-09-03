<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-125 Prefab Runtime 覆盖投影实施记录

## 结果

M-125 已把场景 v4 的 Prefab 字段覆盖接入 Runtime 投影。Scene Instance Service 持有一份注册了
World 内建组件并已冻结的 Type Registry；Prefab 创建和刷新共用同一条覆盖验证与属性写入路径。

## 已验证行为

- 投影先加载来源、创建独立节点与组件、应用实例根 Transform，最后通过属性访问器应用字段覆盖。
- 覆盖写入前验证实例身份、来源节点、Type ID、Field ID、属性类别和可写能力。
- 同一来源的不同实例拥有独立投影，单实例 Transform 覆盖不会污染来源或其他实例。
- 刷新使用作者场景中同一组覆盖创建替代投影，成功后才替换旧投影，覆盖值在刷新后保持。
- 来源节点不存在、字段删除、类型变化或属性写入失败时，临时投影由 RAII 回滚；旧刷新目标不会
  在替代投影成功前被销毁。
- Scene Service 复用单一冻结 Registry，避免为每个 Prefab 实例重复创建反射元数据。

## 验证

Windows Clang Debug 完整构建通过。Prefab Runtime 专项测试覆盖 Transform 覆盖、多实例隔离、
无效来源节点回滚与实体数量不变；Scene Prefab 集成测试覆盖场景加载应用覆盖及刷新保持覆盖。

本阶段没有增加公共 ABI。Editor 的覆盖状态呈现和作者编辑入口由 M-126 接续实现。
