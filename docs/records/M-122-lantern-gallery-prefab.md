<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-122 Lantern Gallery Prefab 复用实施记录

## 结论

Lantern Gallery 已用一个项目自有 `lantern.prefab.json` 取代三份重复灯笼子树。场景 v3 只保存左、
中、右三个实例声明及各自根 Transform；普通场景节点、实例根和来源节点均使用可读名称。

Editor 的刷新操作会按 Prefab URI 更新当前场景内全部同源实例，并作为一条可撤销命令提交。任一实例
刷新失败时，已刷新的实例会回滚；刷新成功后，各实例根名称、父级和 Transform 保持独立。

## 自动验收

- 作者场景包含 7 个普通节点、3 个实例声明；展开后 Editor 可见 15 条 Prefab 节点记录。
- 三个实例共享同一 Prefab URI，保留不同位置与缩放。
- 修改一次 Prefab 来源名称后，三个实例显式刷新均得到新内容；事务切换可恢复旧内容并再次应用。
- 保存并重新打开后仍保留紧凑实例声明、更新后的来源内容和新建普通节点。
- Lantern Gallery 独立示例、Runtime 工程加载和 Editor–Runtime 工作流均通过。

## 验证状态

| 验证 | 结果 |
| --- | --- |
| Windows Clang Debug Lantern Gallery 相关测试 | 5/5 通过 |
| Windows Clang Debug 完整串行回归 | 109/109 通过 |
| Windows Clang Static Debug 完整串行回归 | 106/107 首轮通过；退出协议超时用例单独复测通过 |
| Linux Shared/Static、Granit Runtime、Sanitizer Actions | 待用户授权 |

M-122 的本地实现已经完成；0.18.0 是否结束取决于跨平台 Actions 验收结果。
