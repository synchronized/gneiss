<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-127 Prefab 覆盖恢复与命令历史实施记录

## 结果

M-127 已为 Editor 增加 Prefab 来源节点 Transform 的字段级与整节点恢复操作。恢复通过现有 Scene
作者接口写回来源值，因此对应的稀疏覆盖会被删除，Runtime 投影与 Inspector 状态同步更新。

## 已验证行为

- Inspector 可分别恢复 translation、rotation 或 scale，也可一次恢复完整 Transform。
- 没有对应覆盖时恢复按钮不可用，避免创建无意义命令。
- 每次成功恢复都记录独立 Undo/Redo 命令；撤销恢复作者值与覆盖标记，重做再次删除覆盖。
- 命令使用实例 UUID 与来源节点 UUID 重新定位，不保存可能因 Prefab 刷新失效的 Runtime ID。
- 命令记录失败时立即恢复操作前的 Transform，避免已修改内容脱离历史。
- Editor Session 的恢复接口返回操作前有效值，供 UI 命令历史建立反向操作。

## 验证

Windows Clang Debug 的 `gneiss_editor` 与 `gneiss_editor_session_test` 构建通过；Editor Session 测试
覆盖字段级恢复、整 Transform 恢复、覆盖标记删除及恢复前值回传。

Runtime 属性回写到 Prefab 覆盖由 M-128 接续实现。
