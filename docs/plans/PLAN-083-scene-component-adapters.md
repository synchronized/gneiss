<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-083：场景组件适配注册

状态：已完成。决策见 [ADR-096](../decisions/ADR-096-scene-component-adapters.md)。

## 范围

1. 核查创建、捕获、删除、重载与 Prefab 覆盖中的具体类型判断。
2. 在 Function/Scene 定义按描述 GUID 注册的组件操作；注册结束后只读共享。
3. 迁移相机和网格适配，统一组件差异、安装/捕获、移除、属性覆盖与依赖枚举。
4. 使用真实 ECS 测试组件验证创建、编辑、保存、重载、删除、Prefab 覆盖和生命周期。

Meta 只负责数据反射；World 内部扩展适配可访问现有 ECS，不建立外部实体映射。
保留显式相机/网格便捷接口，不新增插件、Service、第二套 World 或调度框架。

## 验收

Debug 构建及相关 CPU 回归。未知类型/重复注册/冻结后注册有明确错误，捕获失败不覆盖输出。
新增测试组件只增加注册与适配，不修改 Scene/Prefab 核心类型判断。
不运行大场景、GPU、浏览器或远端 CI。

## 完成范围与验证

- 创建、捕获、结构差异、组件替换及失败恢复、Prefab 覆盖与依赖枚举已使用统一适配表。
- 相机和网格具体类型处理集中到内建适配；显式相机/网格编辑接口和子树资源保活规则保留。
- Prefab 重载以当前 ECS 组件与带覆盖的新描述比较，避免遗漏运行中修改。
- `component_adapter_test.cpp` 的测试组件存入 World 现有 ECS，覆盖保存往返、编辑、重载失败恢复、
  组件删除、节点卸载、子树恢复和 Prefab 覆盖；不修改通用 Scene 代码。
- Windows Clang Debug 构建通过，27 项相关 CPU 回归通过，`git diff --check` 通过。
  按本阶段范围未运行大场景、GPU、浏览器与远端 CI。

当前 API 与扩展约束见[场景实例参考](../reference/scene-instance.md)。
