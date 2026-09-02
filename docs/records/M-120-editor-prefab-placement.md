<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-120 Editor Prefab 放置与层级呈现实施记录

## 结论

M-120 已建立首个 Editor Prefab 放置与浏览闭环。Asset Browser 可把作者 Prefab 放到场景根或当前
选中的普通节点下；Hierarchy 独立展示实例根与来源节点，不改变普通作者节点的枚举数量和顺序。

## 接口与边界

- Scene Instance 新增实验性 Prefab 节点枚举和原子实例创建接口，并提供对应 C++20 包装。
- 实例根由实例 UUID 标识；来源节点由实例 UUID 与来源 UUID 组成复合作者身份。
- 来源节点在 Hierarchy 和 Inspector 中明确标记为只读，不提供重命名、拖放、删除或属性编辑入口。
- 当前里程碑允许选择和检查实例根，但根级编辑、命令历史和显式刷新留给 M-121。
- 保存继续只写实例声明，不写展开后的来源节点或 Runtime 标识。

## 验证结果

| 验证 | 结果 |
| --- | --- |
| 普通节点与 Prefab 节点独立枚举 | 通过 |
| 实例根、来源节点身份和只读标志 | 通过 |
| 以普通节点为父级原子放置 Prefab | 通过 |
| 新实例进入脏状态并参与场景序列化 | 通过 |
| Editor Session 选择实例根与来源节点 | 通过 |
| Windows Clang Debug Prefab 与 Editor Session 专项测试 | 4/4 通过 |
| Windows Clang Debug 完整串行回归 | 109/109 通过 |

并行回归曾使两个会启动真实 Runtime 的进程测试互相干扰并超时；两项单独串行复验和完整串行回归
均通过，不属于 Prefab 功能失败。
