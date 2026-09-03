<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-135 Editor Prefab 作者操作实施记录

## 结论

M-135 将 Create Prefab、Apply Overrides 和 Unpack 接入 Editor。三类操作通过确认窗口执行 M-131
原子作者事务，成功后重新加载作者场景并按稳定 UUID 恢复选择，同时登记可持久化的 Undo/Redo。
操作失败会保留或恢复提交前文档，并在 Editor 中显示结果码对应的错误信息。

## 已实现入口

- 普通场景节点的右键菜单提供 `Create Prefab...`，确认前可修改资产根内的来源路径。
- Prefab 来源节点的 Inspector 展示来源 URI、字段覆盖状态和 `Apply Overrides to Prefab...`。
- Prefab 实例根在层级操作区提供 `Unpack Prefab...`，确认文字明确来源与其他实例不受影响。
- 场景存在未保存修改或 Runtime 正在运行时禁用作者操作，避免磁盘基线与运行镜像产生歧义。
- Apply 仅在选中实例确实存在 Transform 覆盖时启用；确认信息提示会刷新全部同源实例。

## 事务与历史

- 作者命令读取磁盘中的精确场景与 Prefab 基线，避免重新序列化导致无意义的修订冲突。
- 提交后完整重载当前作者场景，从而同步 Create、Apply 和 Unpack 对全部节点投影的影响。
- 场景重载失败时立即提交反向事务并尝试恢复旧场景，不留下已知的半完成会话。
- Undo/Redo 使用 M-134 的反向文档变更切换磁盘状态，每次切换后重新加载并恢复对应选择。
- 作者事务每次切换都已持久化，因此不会把成功的磁盘提交误报为待保存场景修改。

## 验证

- Windows Clang Shared Debug 的 `gneiss_editor` 构建通过。
- Editor 使用 `examples/editor_demo` 完成三帧 `--smoke` 启动与关闭检查。
- Session、Command History、Author Transaction 和 Prefab Authoring 相关测试保持通过。
- 新增作者操作实现通过 `clang-format`；对应新增代码未产生 `clang-tidy` 告警。

M-136 下一步通过 Lantern Gallery 完成 Create、修改、Apply、Unpack、Undo/Redo 和保存重开的作者闭环，
并执行 Windows 与远端 Linux 验收矩阵。
