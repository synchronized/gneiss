<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Scene Tree 与 Transform

## 所有权与映射

每个 World 拥有一棵 Scene Tree。节点使用不透明的 `gneiss::entity_id` 标识，零值无效；节点
ID 只在所属 World 的生命周期内有效，销毁后旧 ID 不会指向复用槽位的新节点。

有层级的节点就是同一个 ECS 实体，名称、父子关系和局部变换存放于实体组件。
World 不再分配独立节点槽位，也不维护节点到实体的扫描映射。纯 ECS 实体可以没有层级组件。
删除节点或删除该实体都会递归删除子树中的实体与组件，所有旧节点和实体句柄同时失效。
World 不再提供 node 创建或 node/entity 转换接口；scene_instance::create 返回 game_object。

## 层级操作

创建节点时，父节点为零表示根节点。`game_object::reparent` 保留局部 Transform，并拒绝把节点
挂到自身或后代下面。不同场景的已登记成员不能互相挂接，包含通过中间对象移动子树的情况；
拒绝时不改变原层级。销毁节点会递归销毁整棵子树，所有相关节点 ID 随即失效。

`game_object::parent` 返回当前父对象；根对象成功返回空句柄。节点无效、属于其他 World
或已被递归销毁时返回 `error::invalid_handle`。

Scene Tree 和所属 World 一样只能在 World 的创建线程访问。跨 World 节点、失效节点和重复销毁
均返回句柄错误。

## Transform

`gneiss::engine::math::transform` 包含平移、四元数旋转和缩放，默认构造提供单位变换。
World 的层级组件是 Transform 的唯一权威存储：

- 局部 Transform 相对于父节点；根节点相对于世界原点。
- 世界 Transform 按根到当前节点的顺序组合平移、旋转和缩放。
- 设置操作拒绝 NaN、无穷值、非归一化旋转和零缩放。
- 重挂接保留局部 Transform，因此世界 Transform 可能改变。

四元数长度必须在 `1±1e-4` 范围内，每个缩放轴的绝对值必须至少为 `1e-6`；不满足时返回
`error::invalid_argument`，原值保持不变。负缩放有效，零缩放因不可逆而被拒绝。

`world::entity_get_local_transform` 和 `world::entity_set_local_transform` 直接通过实体组件
访问同一份局部 Transform，不创建第二份状态。实体有效但没有关联节点时返回
`error::not_found`；跨 World 或失效实体返回 `error::invalid_handle`。

层级采用显式 TRS 分量组合：子平移先乘父缩放、再由父旋转变换，旋转按父乘子组合，缩放逐轴
相乘。这一纯 TRS 表达不保存“非均匀缩放后再旋转”可能产生的剪切分量；需要剪切的内容应在 Mesh
顶点中烘焙。重挂接不会尝试反求局部 Transform，也不承诺保持世界 Transform。

当前实现按查询即时计算世界 Transform；脏标记缓存应在真实性能数据证明必要后再加入。

## 内部层级访问

`world_hierarchy::create(parent, entity)` 返回 `result<entity_id>`；
`get_world(node)` 和 `get_world_for_entity(entity)` 返回 `result<math::transform>`。
创建不再接收输出指针，失败不产生节点 ID；变换查询返回副本，失效节点返回 invalid_handle，
非零实体未关联节点返回 not_ready。World 外层接口按其声明保留线程校验。
world_hierarchy 位于 function/scene/detail/hierarchy.hpp，只借用 world_state，不拥有节点存储。
