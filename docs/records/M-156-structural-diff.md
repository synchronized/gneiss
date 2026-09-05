<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-156：身份协调结构差异实施记录

## 结果

新增 Engine 私有纯数据结构差异模型。它以稳定 UUID 对照新旧作者对象，分别输出新增、更新和删除
集合；新增按父节点到子节点排序，删除按子节点到父节点排序，更新按 UUID 排序，因此不依赖 JSON
数组顺序，也不接触 World、实体或资源状态。

更新集合独立标记名称、父级、Transform、Camera 和 Mesh Renderer 变化。输入在生成差异前验证身份
唯一、父节点存在、禁止自引用且层级无环；失败时不保留部分输出。

## 验证

- 无变化输入生成空差异。
- 新增父子节点与删除父子节点分别得到安全的正序和逆序。
- 同一节点的名称、重挂接、Transform、Camera 删除和 Mesh Renderer 新增可同时表达。
- 重复 UUID、缺失父节点和层级环返回 `invalid_argument`。
- Windows Clang Shared Debug 专项构建与 `gneiss.structural_diff` 测试通过。

## 边界

本阶段只计算作者对象差异，不执行资源预取、World 修改或回滚。Prefab 实例容器差异和实际事务提交
分别由后续 Scene、Prefab 里程碑组合。
