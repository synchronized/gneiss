<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Prefab 组件覆盖反射验收

prefab_component_override 直接注册 source_node_uuid、type_guid、added 三个必填成员，
通过严格反射序列化读写。新增组件位于 added 的多态信封，删除组件明确写 added: null。
拒绝旧的同层信封、缺失字段和未知字段，不提供旧格式兼容。

删除 make_component_overrides、parse_component_override、read_component 及组件覆盖专用
encode/decode。对象和类型非空、新增组件类型匹配等局部规则独立注册为校验；
覆盖重复与跨列表冲突继续由 Prefab 引用校验处理。属性覆盖 codec 保留。
当前协议见[Prefab 组件结构覆盖](../reference/scene-format.md#prefab-组件结构覆盖)。

Windows Clang Debug 全量增量构建、8 项相关测试及核心分层检查通过。
测试覆盖新增/删除往返、漏写 added、旧格式拒绝、空新增引用、类型不匹配及编辑器覆盖操作。
git diff --check 通过。未运行大场景、完整测试矩阵或远端 CI。
