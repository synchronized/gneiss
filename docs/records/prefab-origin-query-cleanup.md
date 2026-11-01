<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Prefab 来源查询与覆盖状态收敛

2026-10-31，移除 `scene_prefab_flags`，将来源身份与属性覆盖查询分开。

## 当前行为

- `prefab_origin_view::is_instance_root` 表达实例根身份，不代表操作权限。
- 来源查询借用实例的实际覆盖列表；`has_override(type, property)` 按源节点、类型与
  顶层属性筛选，包含该属性下的嵌套路径覆盖。视图有效期到下次场景修改或销毁。
- 编辑器只保存当前节点覆盖键的 UI 快照，不重复保存覆盖值；Transform 修改后重新
  从场景查询刷新快照，不再根据变换差异推算三个固定标记。
- 删除 `is_read_only`。内部节点删除、重命名等结构限制继续由 Scene 操作接口校验；
  属性覆盖编辑不受笼统只读标志限制。没有改变文件格式或引入新的持久化状态。

## 验证

Windows Clang Debug 全量构建通过，11 项相关 CPU 测试通过，覆盖来源查询、非 Transform
类型的覆盖匹配、编辑器会话、属性编辑、恢复、撤销重做、Prefab 重载与结构操作。
源码和测试中无旧 flags 引用，`git diff --check` 通过。

未运行 GPU、大场景、浏览器或远端 CI 验证。
