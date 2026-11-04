<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# ADR-101：组件检查复用描述与适配器

- 状态：已接受。
- 日期：2026-10-31。

## 决策

World 实际组件决定存在性。场景查询与宿主检查使用已有 component_descriptions 自有快照，
不定义组件位标记、逐类型可选字段或第二套组件注册表。Transform 保持节点公共数据。
Meta 访问快照，不直接暴露 ECS 内存；普通组件读写通过组件适配器验证和提交。
组件属性分派属于 function/scene 适配层，World 仅提供实体到节点的查询，不反向依赖适配器。
IPC 检查域版本升级到 2，统一传输类型 GUID 与反射编码内容，删除旧字段而不双写。
宿主协议目标依赖既有内部 native interface，不发布公共 C++ SDK。

## 影响与限制

新增可检查组件只需注册反射与适配器。快照捕获有复制开销；目前不引入专门驻留缓存。
未注册或重复类型的 IPC 内容拒绝解码，双方需具备对应类型注册。
远程写入沿用现有顶层标量协议，嵌套与容器属性可展示但暂不远程修改；本地编辑命令不受此限制。
MeshRenderer 可以存在而资源槽为空，依赖加载忽略空槽，渲染跳过未就绪项。

实施与验收见 [PLAN-096](../plans/PLAN-096-component-inspection.md)。
