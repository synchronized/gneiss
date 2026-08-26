<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Scene Tree 与 Transform

## 所有权与映射

每个 World 拥有一棵 Scene Tree。节点使用不透明的 `gneiss_scene_node_id` 标识，零值无效；节点
ID 只在所属 World 的生命周期内有效，销毁后旧 ID 不会指向复用槽位的新节点。

节点可以关联至多一个 Entity，同一 Entity 也只能关联一个节点。Scene Node 不拥有 Entity：递归
销毁节点不会销毁关联实体；销毁实体则会自动清除节点上的关联。这一单向关系避免 Scene Tree 与
ECS 之间形成循环所有权。

## 层级操作

创建节点时，父节点为零表示根节点。`gneiss_scene_node_reparent` 保留局部 Transform，并拒绝把节点
挂到自身或后代下面。销毁节点会递归销毁整棵子树，所有相关节点 ID 随即失效。

Scene Tree 和所属 World 一样只能在 World 的创建线程访问。跨 World 节点、失效节点和重复销毁
均返回句柄错误。

## Transform

`gneiss_transform` 包含平移、四元数旋转和缩放。`GNEISS_TRANSFORM_IDENTITY` 提供单位变换。
Scene Tree 是层级 Transform 的唯一权威存储：

- 局部 Transform 相对于父节点；根节点相对于世界原点。
- 世界 Transform 按根到当前节点的顺序组合平移、旋转和缩放。
- 设置操作拒绝 NaN 和无穷值。
- 重挂接保留局部 Transform，因此世界 Transform 可能改变。

当前实现按查询即时计算世界 Transform；脏标记缓存应在真实性能数据证明必要后再加入。
