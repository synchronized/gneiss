<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-110 定向 CPU 验收

## 范围与环境

2026-11-10，Windows Clang Debug，使用 `windows-clang-debug` 构建和测试 preset。
本次验证 [PLAN-110](../plans/PLAN-110-world-scene-ownership.md) 的对象生命周期、组件行为及编辑投影链路。
不运行 GPU、交互窗口、大场景基准或远端 CI；不代表跨平台或发布矩阵验收。

## 检查范围

- World/Scene：`native-world-lifecycle`、`native-scene-lifecycle`、`scene_tree_transform`、
  `world_state`、`scene_instance`，覆盖失效句柄、错误线程、父子关系、场景归属和子树迁移。
- 组件：`component_reflection`、`component-adapter`，覆盖重复新增、删除析构、相机校验、
  属性编辑失败不提交，以及更新回调保留描述外运行状态。
- 场景装配：`scene_structural_reload`、`scene-instantiation`、`scene_prefab_instance`、
  `prefab_structural_reload`、`prefab-runtime-application`，覆盖装配、来源地址、增量更新及重载。
- Runtime/Editor：`runtime.scene-inspection`，以及 `editor.` 前缀的 `command-history`、
  `prefab-edit`、`prefab-authoring`、`runtime-scene-mirror`、`document-editing`、`document-actions`、
  `component-edit`、`component-lifecycle`、`prefab-component`、`prefab-author-operations`、
  `scene-document-edit`、`scene-document-snapshot`、`scene-document-versions`、
  `author-property-edit`、`asset-scene-commands`，覆盖撤销重做、组件身份、来源映射与未修改对象的身份保留。

以上测试名称统一带 `gneiss.` 前缀，共 28 项。

## 发现与修复

首轮 25 项通过，3 项失败，定位到两个测试问题：

1. `scene_access::get_node_info` 返回的测试快照使用 `string_view` 指向局部名称字符串，
   返回后悬空。将 `scene_node_info::name` 改为自有 `std::string`，与其快照职责一致。
   `scene_instance` 和 `prefab_structural_reload` 的名称检查恢复正常。
2. `prefab-runtime-application` 将同源实例的名称误当成来源身份，要求两者相等。
   改为检查各自场景描述指定的 `First` 和 `Second`，继续通过来源地址及实体 ID 验证实例隔离。

不修改生产运行语义，不关闭断言或跳过失败用例。失败的 3 项单独复测通过。

## 最终结果

最终三组 28/28 项通过，总耗时 28.01 秒；默认构建通过。PLAN-110 本次定向 CPU 验收完成。
本地日志位于
`build/plan110-regression-fix-build.log` 和 `build/plan110-final-cpu-tests.log`。
