<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-136 Lantern Gallery Prefab 作者闭环验收记录

## 结果

Lantern Gallery 的 Prefab 作者闭环、Windows 本地矩阵和 Linux 跨平台矩阵均已通过，M-136 与
0.20.0 Prefab 作者工作流完成验收。

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

## 跨平台验证

- Linux Actions [运行 33730410727](https://github.com/synchronized/gneiss/actions/runs/33730410727)
  在提交 `5731f820f276fbd4d6c5726d824f3474e107bf05` 上通过。
- GCC 与 Clang 的 Shared/Static 四种 Core 组合全部通过配置、构建与测试。
- Granit Runtime Shared/Static 均通过构建及无头窗口测试。
- Sanitizer Runtime 通过内存错误、未定义行为和退出泄漏检查。
