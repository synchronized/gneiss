<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-136 Lantern Gallery Prefab 作者闭环验收记录

## 当前状态

Lantern Gallery 的本地作者闭环与 Windows Clang Shared/Static 验收已完成。Linux Shared/Static、
Granit Runtime 和 Sanitizer 仍需在当前分支推送后手动触发 Actions 验证，因此 M-136 暂处于
“本地完成，待跨平台验证”。

## 作者闭环

新增 `gneiss.editor.lantern-prefab-authoring-workflow` 集成测试。测试复制构建后的真实 Lantern
Gallery 工程到临时目录，并依次验证：

1. 把普通石柱节点创建为独立 Prefab，提交后重开场景。
2. 将左侧灯笼的 Transform 覆盖应用回灯笼 Prefab，并确认三个同源实例均属于影响范围。
3. 把中心灯笼 Unpack 为普通作者节点，保留可见层级与字段值。
4. 对 Unpack 执行 Undo 和 Redo，每一步均从磁盘重新加载场景，验证事务结果可持久化。

所有操作仅发生在临时工程副本中，不修改仓库的示例资产。

## 本地验证

- Windows Clang Debug 全量测试通过，共 113 项。
- Windows Clang Static Debug 全量测试通过，共 111 项。
- 新增闭环测试在 Shared 与 Static 两种链接方式下均通过。
- Static 构建继续出现既有的 Ninja 构建日志恢复提示与第三方 libuv 编译警告，未影响构建及测试
  结果；Gneiss 自有目标仍保持警告视为错误。

## 待完成验证

推送当前分支并手动运行 Linux Actions，确认以下任务全部通过后，才能关闭 M-136：

- GCC 与 Clang 的 Shared/Static Core 矩阵。
- Granit Runtime Shared/Static 无头图形矩阵。
- Sanitizer Runtime 内存错误、未定义行为和退出泄漏检查。
