<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景加载、实例、序列化与卸载

## 加载阶段

`engine::scene_instances::load` 由所属 Application 的原生场景服务接收规范场景 URI，在 Application 创建线程同步执行：

1. 通过 VFS 读取并完整校验场景描述。
2. 获取场景引用的全部 Mesh 与 Material 缓存租约。
3. 父节点优先创建 Entity 和 Scene Node，再设置 Transform 与组件。
4. 获取场景声明的 Prefab，并在实例根下创建各自独立的源节点 Runtime 投影。
5. 全部成功后才发布非零场景实例句柄。

解析或资产阶段不会修改 World。提交阶段任一步失败都会逆序销毁本次创建的实体、节点和资产引用，
因此失败前后 World 实体数量与资源存活状态一致。当前加载只允许 Application 创建线程调用。

Runtime 宿主的启动与完整场景切换、Editor 的启动与显式打开场景使用内部异步协调器，行为见
[调度指南](../guides/task-scheduling.md#runtime-完整场景加载)。它不会改变上述原生同步接口；
异步候选加载期间，宿主的结构/资产替换返回 `GNEISS_ERROR_NOT_READY`，调用方应等待当前事务结束。
完整切换后应重新查询活动 World 与 Scene，不继续使用旧场景借出的节点、Entity 或字符串视图。

`engine::scene_instances::create_empty` 使用调用方提供的小写规范场景 UUID 创建不含节点的作者实例，
不读取 VFS，也不关联来源 URI。它用于 Editor 的未命名新场景；后续节点编辑和序列化行为与加载
实例一致，实际写入路径仍由 Editor 的 Save As 生命周期负责。

## 所有权与卸载

场景实例由所属 Application 独占，不能跨 Application 使用。`engine::scene_instances::unload` 依次释放
实体、节点和资产租约，句柄立即失效；重复卸载返回句柄错误。Application 销毁时会先卸载仍存活的
场景，再销毁 World 和 Render Resource Service。

场景中的 Mesh Renderer 只借用 RID，资源生命周期由场景实例持有的租约保证。

## UUID 查询

`engine::scene_instances::find_node` 使用场景文件中的规范对象 UUID 查找借用 Scene Node ID。找不到返回
`GNEISS_ERROR_NOT_FOUND`。节点 ID 只在场景实例未卸载且 World 存活期间有效，可用于运行时更新
Transform；持久化文件仍不得保存节点 ID。

## 节点枚举

`engine::scene_instances::get_node_count` 与 `engine::scene_instances::get_node_info` 按作者场景 `objects`
数组的稳定顺序枚举实例节点。节点描述包含当前 Node ID、父 Node ID、Entity ID、规范 UUID、可选
显示名称、当前局部 Transform、组件标志、Camera 作者值以及可选 Mesh/Material 作者 URI。作者
JSON 可为对象增加可选字符串 `name`；旧场景无需迁移，名称为空时由调用方选择 UUID 等回退显示
文本。查询输出使用原生 scene_node_view，不再提供版本化 C 布局。

节点描述中的 UTF-8 文本通过 string_view 借用到下次场景修改或父对象失效；跨修改保存时
必须复制字符串。索引越界返回 result::not_found，节点或实体失效返回句柄错误。


Prefab 使用独立的原生枚举接口 `engine::scene_instances::get_prefab_node_count` 与
`engine::scene_instances::get_prefab_node_info`，不改变普通 `objects` 的数量和顺序。枚举顺序为每个实例
的实例根，其后为 Prefab 来源节点；描述包含实例 UUID、来源节点 UUID、Prefab URI、Runtime ID、
局部 Transform 以及实例根或来源只读标志。字符串同样只借用到下一次场景修改或父对象失效；
Prefab 刷新、撤销/重做和重命名之后应重新枚举，不能沿用旧字符串指针。

`engine::scene_instances::create_prefab_instance` 在普通作者节点或场景根下原子放置 Prefab。调用方提供
唯一实例 UUID、显示名称、规范 Prefab URI 和实例根 Transform；资源获取或 Runtime 创建失败时不会
增加作者声明或留下部分节点。当前接口不允许以 Prefab 节点作为父级。

`engine::scene_instances::set_prefab_source_transform` 修改来源节点的实例局部 Transform，并把与来源值
不同的字段保存为稀疏覆盖；恢复为来源值时自动删除对应字段覆盖。三个 TRS 字段先在临时作者集合中
完成校验，Runtime 写入成功后才提交作者覆盖，因此失败不会发布部分作者状态。该接口不修改 Prefab
来源资产，也不会影响同源的其他实例。

Prefab 来源节点枚举结果通过 translation、rotation 与 scale 三个覆盖标志报告字段状态，并同时提供
`source_local_transform` 与当前 `local_transform`。调用方可据此呈现来源值和实例有效值；标志只描述
作者覆盖，不表示 Runtime 是否正被临时调试命令修改。

实例根可通过 `engine::scene_instances::set_prefab_instance_name` 修改作者名称，通过普通 Scene Node
Transform 接口修改根变换，并通过 `engine::scene_instances::destroy_prefab_instance` 整体销毁。
`engine::scene_instances::refresh_prefab_instance` 会绕过旧 Prefab 缓存重新读取来源，先创建完整替代投影，
成功后才销毁旧投影并返回新的根 ID；失败时旧投影和选择目标仍有效。刷新成功后旧根及全部旧来源
节点 ID 失效，调用方必须改用返回的新根并重新枚举层级。成功刷新同时返回场景实例独占的事务
令牌；`engine::scene_instances::toggle_prefab_refresh` 使用令牌在新旧来源版本间切换并返回新的根 ID，
供 Undo/Redo 使用。命令离开历史后必须调用 `engine::scene_instances::release_prefab_refresh`；场景卸载
也会释放尚未显式释放的令牌及其资产租约。

C++ `scene_prefab_refresh` 独占令牌并记录所属 Application 与场景句柄，不延长父对象寿命。
`scene_instance::refresh_prefab_instance(root, out_root, out_refresh)` 的输出拥有者必须为空；
非空时返回 `invalid_state`，不修改场景或两个输出。空输出下的加载失败也保持输出与原投影。
成功后可以用 `out_refresh.toggle(out_root)` 撤销或重做；失败时保留输出根 ID。
空令牌调用 toggle 与 C API 一样返回 `invalid_argument`。

拥有者不可复制，可移动；`reset()` 和析构释放历史租约，不回滚当前投影。外部释放、场景卸载或
Application 销毁后，`reset()` 可安全清空失效令牌；其他错误保留令牌供所属线程重试。
操作和析构须在 Application 所属线程，析构/移动覆盖无法关闭时终止进程。
`release()` 转移手动释放责任，调用前保存 `owner()` 和 `scene()`。原裸令牌包装仍保留供旧调用方使用。

## 作者节点编辑

`engine::scene_instances::create_node` 原子创建不含可选组件的通用作者节点。调用方提供稳定 UUID、可选
名称、实例内父节点和局部 Transform；创建失败不会留下 Entity、Scene Node 或作者对象。

`engine::scene_instances::set_node_name` 同步修改运行时显示名称和作者名称；空名称表示清除。
`engine::scene_instances::reparent_node` 同步修改 Scene Tree 和作者父 UUID，支持挂到根，拒绝自身、循环
关系和实例外父节点。两项操作均先准备可能分配的作者数据，再修改运行时状态，失败时保持原状态。

`engine::scene_instances::create_mesh_renderer_node` 原子创建带 Mesh Renderer 的作者节点。调用方提供
稳定 UUID、可选名称、实例内父节点及 Mesh/Material URI；实现会先获取两个资产租约，再创建 Entity、
Scene Node 和 ECS 组件。任何阶段失败都不增加作者对象或泄漏资源。

`engine::scene_instances::set_mesh_renderer` 原子替换实例节点的 Mesh 与 Material。新资产全部获取成功且
ECS 组件更新成功后，才替换作者 URI 和旧租约；失败保留原引用。父节点必须属于同一场景实例，节点
ID 和实例不能跨 Application 使用。这两项操作仅限 Application 创建线程。

`engine::scene_instances::destroy_node` 删除没有子节点的作者节点，并使对应 Entity、Scene Node ID 与
资产租约失效。有子节点时返回 `GNEISS_ERROR_INVALID_STATE`，未知节点返回句柄错误。

## 子树快照、复制与恢复

当前子树接口仅处理普通作者节点。场景其他位置的 Prefab 不进入子树快照；子树自身挂有 Prefab
引用时，捕获与删除返回 `GNEISS_ERROR_UNSUPPORTED`，不改变场景。恢复含 Prefab 引用的快照也
返回该错误，避免只恢复普通节点而遗漏 Prefab。单节点删除同样把挂接的 Prefab 视为子节点，
返回 `GNEISS_ERROR_INVALID_STATE`；可先通过专用 Prefab 接口处理实例，再操作普通作者子树。

`engine::scene_instances::capture_subtree` 输出当前版本场景 Schema 的 UTF-8 JSON 值快照，包含目标根、
全部后代、层级、Transform、Camera 和 Mesh Renderer 作者值，不包含 Entity ID、Scene Node ID、RID
或组件地址。原生接口一次写入调用方提供的 std::string；快照用于当前版本 Editor 命令，不作为独立
资产格式或长期存档格式。

快照根节点的 `parent` 为 null，不保留快照外的父引用；恢复时使用显式 `parent` 参数重新挂接，
局部 TRS 保持不变。这使非根子树快照也能独立通过场景 Schema 校验。

单个快照最多包含 `GNEISS_SCENE_SUBTREE_MAX_NODES`（4096）个节点，超过限制返回
`GNEISS_ERROR_UNSUPPORTED`，防止命令历史无界持有场景副本。

`engine::scene_instances::restore_subtree` 在指定实例父节点下恢复快照。UUID 映射为空时保留原 UUID，
适合删除撤销；非空映射必须完整覆盖全部快照节点，源 UUID 和目标 UUID 均唯一，适合确定性复制。
复制 Camera 时不会复制活动 Camera 身份，避免一个实例出现多个主 Camera。恢复会先完成 JSON、UUID、
父级和资产校验并预取全部资源，再创建 Runtime 投影；失败不会增加作者对象、Runtime 对象或租约。

`engine::scene_instances::destroy_subtree` 显式删除根及全部后代，不依赖调用方逐叶删除。成功后旧 Entity
ID 和 Scene Node ID 全部失效；调用方应在删除前保存快照和根节点原父 UUID，供 Undo 使用。原生接口
分别使用 `std::string` 和 `std::span<const engine::uuid_mapping>` 承载值快照与 UUID 映射。

创建、重命名、重挂接、替换与删除会进入后续
序列化结果，但不会自行写入来源文件；Editor 仍负责脏状态和原子保存。

## 运行时属性序列化

`engine::scene_instances::serialize` 将实例当前的名称、父级、局部 Transform、Camera、已创建作者节点和
Mesh Renderer 引用以及 Prefab 实例根 Transform 合并回加载时的作者文档，并输出当前版本的 UTF-8
场景 JSON。Prefab 仍以 URI、实例 UUID、父级、名称和根 Transform 表示，不写入展开节点。保存会
保留未被 Runtime 解释的未知字段，不会覆盖来源文件，也不会持久化 Entity ID、Scene Node ID 或
资源 RID。

原生接口一次写入调用方提供的 `std::string`，无需预先查询长度或管理 C 缓冲区。

序列化只允许在所属 Application 创建线程执行。实例中的实体、节点或原有 Camera 已被调用方删除
时返回对应错误且不产生部分输出。输出文本如何落盘由工具或宿主负责；只读 VFS 不会被保存操作
隐式修改。

## C++ 原生描述

`engine::node_creation`、Prefab 和组件描述使用 `string_view`、强类型 ID 和原生变换/相机配置，
默认构造即可使用，不填写布局大小或字符串计数。输入文本借用至方法返回。
节点查询输出使用 `scene_node_components`，Prefab 标志使用 `scene_prefab_flags`；
二者支持位运算和 `has_flags`，不能混用。输出文本借用至下次场景修改或父对象失效，
失败查询保持原输出。跨修改持有名称时应复制为 `std::string`。
