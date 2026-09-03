<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-126 Editor Prefab 覆盖编辑实施记录

## 结果

M-126 已允许 Editor 编辑 Prefab 来源节点的局部 Transform。Inspector 和 Transform Gizmo 均通过
Scene 作者接口写入实例覆盖，不直接把临时 Runtime 修改伪装成作者数据。

## 已验证行为

- Prefab 来源节点仍不能改名、改父级或增删组件，但可以编辑 translation、rotation 与 scale。
- Inspector 分字段显示 `Inherited` 或 `Overridden`，并同时展示 Prefab 来源 TRS，便于识别差异。
- Transform Gizmo 支持普通作者节点和 Prefab 来源节点，后者使用实例 UUID 与来源 UUID 重新定位。
- Inspector 与 Gizmo 的编辑均进入现有命令历史，Undo/Redo 使用稳定作者身份，不依赖旧 Runtime ID。
- Scene API 先准备稀疏覆盖集合，再写入 Runtime；无效 Transform 不会提交作者覆盖。
- 写回来源值会删除对应字段覆盖，编辑其他字段不会强制保存完整 Transform。

## 验证

Windows Clang Debug 完整构建通过；Scene Prefab 集成测试覆盖创建、稀疏删除、再次覆盖和刷新保持，
Editor Session 测试覆盖来源节点编辑及覆盖状态同步。

字段级和节点级显式“恢复来源值”操作以及更完整的历史行为由 M-127 接续实现。
