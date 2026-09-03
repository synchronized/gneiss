<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-132 从场景子树创建 Prefab 实施记录

## 结论

M-132 建立了 Editor 私有的 Create Prefab 作者命令核心。命令从场景 v4 作者 JSON 提取指定普通节点
及其全部普通后代，一次生成“新建 Prefab 来源”和“场景子树替换为 Prefab 实例”两项文档变更，
并交由 M-131 作者事务原子提交。当前里程碑不提供界面入口，交互与故障提示由 M-135 统一接入。

## 已验证行为

- Prefab 保留子树节点 UUID、显示名称、组件数据、相对层级及未知对象字段。
- 原场景保留未选中的普通节点、已有实例和未知场景字段。
- 子树根的局部变换移动到新实例根，Prefab 来源根归一化为单位变换，保持原有世界变换。
- Prefab 文件路径必须与场景记录的 `asset://` URI 一致，且实例 UUID 不得与现有对象或实例重复。
- 选中节点不存在、目标参数无效或子树包含已有 Prefab 实例时，不生成任何文档变更。
- 反向事务严格交换基线与目标，可同时恢复原场景并删除新建 Prefab 文件。

## 验证

- Windows Clang Shared Debug 专项构建及 `gneiss.editor.prefab-authoring` 测试通过。
- 生成后的场景与 Prefab 已通过真实 Application 和 Scene Instance 重新加载验证。
- M-131 作者事务共同覆盖成功提交、Undo 删除、路径约束与冲突拒绝。
- 新增源码通过 `clang-format` 和 `clang-tidy`。

Editor 菜单、目标路径选择、命令历史注册与执行反馈将在 M-135 中接入；M-133 下一步复用相同事务
边界实现 Apply 到来源及全部同源实例刷新。
