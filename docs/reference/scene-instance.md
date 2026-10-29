<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景加载、实例、序列化与卸载

## 运行期身份与所有权

`scene_instances` 拥有 `scene_instance`，对外使用非拥有的 `scene_instance_id`。
节点统一使用 `scene_node_id`，校验所属域和槽位代次；不能跨 World 使用，也不保活对象。
持久化身份使用 UUID，文件不保存运行期 ID。

World 拥有场景树和 ECS；场景实例记录自己的全部节点并负责统一删除。
ECS 组件持有 mesh/material 等引擎资源强引用，不保存第三方后端对象。
`prefab_binding` 保存模板资源与来源 UUID 到实际节点的非拥有映射，借用 World 和资源服务。
它不在析构时销毁节点，也不保留创建期间的有效描述副本；投影由无状态的 `prepare_prefab_description` 处理。

## 加载阶段

文件读取和依赖准备由异步加载协调器完成，见[调度指南](../guides/task-scheduling.md)。
`scene_instances::instantiate_prepared` 只消费已准备的描述和资源引用，在 World 所属线程同步创建：

1. 按唯一 `objects` 列表计算父节点优先顺序。
2. 普通对象直接创建节点、实体和组件。
3. 带 `prefab` 的对象以模板根创建实例根，再创建模板子节点和应用覆盖。
4. 全部成功后返回场景实例 ID；失败回收本次创建的节点及 ECS 组件。

同步创建阶段不跨帧，也不支持中途取消；文件和资产准备阶段可取消。
`create_empty` 创建无节点场景，不读取文件。`capture_prepared` 捕获内存描述及已加载资源引用，
可供编辑器恢复使用；它不读取文件，也不创建隐藏的备份实例。

## 对象与来源查询

`get_node_count`、`get_node_info` 枚举所有实际节点，返回 `scene_node_view`。
显式对象排列在前，与描述列表一致；模板生成的子节点排列在后。不能依赖一个实例的节点连续排列，
也不能跨修改保留枚举下标。查询值包含节点、父节点、实体、变换、组件信息和可选 `prefab` 来源信息。
字符串视图仅借用到下一次场景修改或所属对象失效；跨修改保存时必须复制。

`find_node` 按显式对象 UUID 查找普通对象或 Prefab 根；模板子节点通过来源信息中的
实例 UUID 与来源 UUID 区分。同一模板的两个实例拥有不同节点和实体，可以共享模板资源。

## 编辑和保存

`object_description` 只有一份身份、名称、父关系和变换；可选 `prefab` 保存 URI 与相对覆盖。
没有独立的实例描述数组。根名称与变换采用外层对象值，根组件来自模板。
具体格式与校验见[场景格式](scene-format.md)。

`set_node_name` 支持普通对象与实例根。`reparent_node` 支持显式对象，普通自有对象可挂在实例根下，
不能挂到模板内部节点。模板内部结构暂不提供增删或重排覆盖。
`destroy_node` 对所有显式对象统一要求没有实际子节点；完整删除使用 `destroy_subtree`，不按 Prefab 类型切换删除语义。
普通组件编辑接口仍限制为自有对象；来源 Transform 编辑通过 `set_prefab_source_transform` 验证并记录稀疏覆盖。
恢复来源默认值时移除对应覆盖。直接修改 World 组件不会自动形成持久化覆盖。

`capture_description` 采集显式对象，`serialize` 编码为当前格式；来源生成节点不重复输出。
未知 JSON 扩展字段按对象 UUID 对齐保留，不按旧数组下标绑定。
`capture_subtree`、`restore_subtree` 处理普通自有子树；含模板引用的子树快照仍返回 `unsupported`，
Prefab 编辑使用专用来源快照。该快照尚不包含实例根下的自有子对象，因此可撤销删除在提交前检查
这些子对象并返回 `unsupported`，不执行不完整的删除；放置命令的撤销同样执行该检查。`destroy_subtree` 则统一删除整棵子树，包括来源关系与 ECS 组件。

## 来源重载与显式刷新

`reload_loaded` 应用已加载场景描述；当前要求 Prefab 引用声明保持一致，来源变更另走
`reload_prefab_loaded`。来源重载按 UUID 匹配节点，保留匹配节点身份、实例根值和合法覆盖。
原地重载不允许更换模板根 UUID；失去目标的覆盖返回错误，不静默绑定到其他节点。

显式刷新通过 `prefab_placement` 准备来源快照，再调用 `instantiate_prefab_ready` 替换。
先删除旧实例，失败不恢复旧实例，也不保证同源多个实例一起成功。
挂在根下的自有对象会暂时移到原实例父节点，刷新成功再挂回新根，保持身份及局部变换；
刷新失败时仍保留在原实例父节点下，不随旧模板销毁。
编辑器历史保存描述和资源强引用，撤销／重做重新创建节点，不保留隐藏实例或刷新令牌。
刷新、撤销或重做后必须重新查询节点 ID 与字符串视图。

## 卸载

`unload` 删除场景全部节点及实体并释放来源引用；场景 ID 立即失效，重复卸载返回句柄错误。
运行时先清理场景实例，再销毁其借用的 World 和资源服务。
GPU 资源实际回收遵守资源服务及渲染后端的延迟销毁规则，不由场景直接释放后端句柄。
