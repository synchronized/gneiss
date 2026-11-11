<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# World 与 Entity

## World 生命周期

`gneiss::create_native()` 返回 `result<world_owner>`，成功值独占一个新 World。
每次调用创建独立 World，不接收已有拥有者；调用方成功后可显式移动拥有权。
拥有者在创建线程释放时销毁 World 及全部实体、组件。
World 不再登记全局 RID 表，不提供借用 RID 或 world_service 转发接口。
工厂仅分配节点与实体校验所需的唯一所属域，域编号不会复用。

World 及其实体只能在创建线程访问；跨线程操作返回 `error::invalid_state`。
内部直接借用 world&；Application 的 native_world() 返回活动对象的借用指针。
场景切换或关闭后必须重新获取，禁止解引用旧指针；借用者必须在 World 销毁前清理。
反射组件入口也直接接收 world&，不再按句柄查回对象。

## 场景所有权与借用

`world::create_scene()` 创建实例槽位并返回借用句柄，不需要描述 UUID 或资产管理器。
World 直接管理槽位与 ECS 成员，没有公开场景集合或 attach 步骤。
World 析构释放实体及组件引用；Runtime 场景协调直接持有 world_owner。

`scene_instance::borrow(world, id)` 返回非拥有的场景句柄。
size、visit_objects 和 unload 直接访问 World 的成员归属和槽位，校验所属线程与场景 RID 的 domain/generation。
复制句柄不延长场景或对象寿命；卸载后副本失效，槽位复用不会恢复旧句柄。
World 销毁后禁止再访问场景句柄，is_valid 也不能检测已经析构的 World。

普通对象的名称、父子、局部变换和场景归属统一保存在私有 object_state 中。
纯 ECS 辅助实体仅按需保存私有 auxiliary_scene_membership；成员按 ECS 组件遍历，不维护第二份可变成员清单；
实例化回执显式返回来源地址到实体的关联；回执及投影关联只保存一个 entity 字段。
编辑器独立维护投影映射，不从运行来源信息反查。
World 拒绝不同场景已登记成员之间的重新挂接，返回 invalid_argument；
检查目标祖先和移动子树，不能通过未登记的中间对象绕开该限制。
场景访问入口统一校验线程和句柄，并清理已经由 World 销毁的成员；
size 和枚举不再返回已经失效的成员。存活实体仍以 World 的 generation 校验为准。
运行对象不提供来源 UUID 查找；来源由实例化回执和调用方映射维护。
普通对象统一通过 scene_instance::create_object 创建并返回 game_object，创建成功即具有场景归属。
普通组件通过有效 game_object 的 try_component<T>() 借用，
add_component<T>() 新增、remove_component<T>() 删除。
当前内置实例化支持 camera_component 和 mesh_renderer_component。需要副本时显式复制借用对象；
资源强引用正常保活，修改副本不改变 ECS。普通组件可以直接赋值，受约束组件通过自身方法修改；
修改 ECS 中的借用组件后调用 notify_component_modified<T>()。
新增和删除对跨 World 或失效实体返回 invalid_handle，错误线程返回 invalid_state。
组件实现单元可包含 detail/component_storage.hpp 并显式实例化对应类型，使用方不依赖 EnTT。
场景归属不允许通过普通组件写入口修改。

借用接口位于 `function/scene/scene_instance.hpp`。
`scene_instance::create_object(options, parent)` 显式创建并登记本场景对象，
返回 game_object；空父对象创建根，其他场景或 World 的父对象被拒绝。
Prefab 展开的成员同样可以作为动态对象的父节点；运行结构修改不写入编辑文档。
不提供 World 级的无归属对象创建入口。动态对象脱离父节点后仍属于原场景，随场景卸载。
纯 ECS 辅助实体的创建与销毁仅保留在 World 私有实现中，由内部创建方明确管理生命周期。

## Entity 生命周期

`game_object::entity()` 返回运行时 `gneiss::entity_id`。实体 ID 包含 World domain 和 EnTT
实体 generation，但编码布局不是公共 ABI：

- 零值表示无效实体。
- 实体只能用于创建它的 World。
- 销毁后旧 ID 失效，即使底层槽位被复用也不会指向新实体。
- 实体 ID 不能持久化为场景对象身份。

`world::find(entity).is_valid()` 查询借用句柄是否有效；对象销毁统一使用 `game_object::destroy()`。
对失效或其他 World 的实体执行销毁、读写组件等操作返回 `error::invalid_handle`。


`world::entity_count()` 返回 `result<std::uint64_t>`；成功的零值与跨线程访问失败分别表达。

## Game Object 借用

`scene_instance::create_object(options, parent)` 创建该场景中的对象并返回 `result<game_object>`。
`world::find(entity)` 只构造借用句柄，零 ID 返回空句柄；各操作再校验线程、所属域和代次。
`world::object(entity)` 立即校验并借用已有对象，纯 ECS 实体返回 not_found；失效或跨 World 实体返回 invalid_handle。
`game_object` 只保存 world* 和 entity_id，复制不保活实体，不保存组件地址。
World 必须比所有句柄的使用期更长；World 销毁后禁止再调用句柄。

name 返回借用名称，要求对象有效且在所属线程访问；local_transform、world_transform 和 parent 返回结果；set_name、
set_local_transform、reparent 和 destroy 修改 World 中的同一实体。
name 返回的名称是借用视图；对象删除、改名或 World 结构变化后须重新查询，
不能假设其他对象增删不会移动 ECS 组件。
根节点的 parent 返回成功的空句柄。reparent 接受空父节点、拒绝跨 World 和循环关系。
重复指定同一父节点是无副作用操作。destroy 删除整棵子树，复制的借用句柄同步失效。
层级、局部/世界变换及对象销毁已通过 game_object 操作；World 的对应 node_* 转发接口已删除。
World 不提供 entity_create、entity_destroy 或 entity_is_alive 等重复生命周期入口。

## 对象遍历与组件借用

scene_instance::objects()/roots() 和 game_object::children() 返回 result<vector<game_object>>。
这是句柄快照，不保活对象；遍历期间可以修改场景，使用旧句柄前应重新检查 is_valid()。
场景对象顺序不保证稳定，children 保留当前子节点顺序。复制快照有分配开销；
不需要修改结构时可使用 visit_objects，回调期间禁止修改结构。辅助实体不作为 GameObject 返回。

try_component<T>() 返回借用指针，缺失组件返回 nullptr；要求对象有效且在所属线程使用。
组件增删或替换、对象销毁、场景卸载后必须重新获取指针，不能把指针跨结构修改保存。
指针写入本身不发送通知；完成修改后调用 notify_component_modified<T>()。
该函数校验所属线程、对象存活和组件存在；不存在返回 not_found。增删组件自动记录变化。
相机的 settings() 只读，set_settings() 校验参数并在失败时保留原值。
默认构造的相机有效；create(settings, primary_hint) 返回 result<camera_component>。
删除 read_component/set_component，不保留副本读取或安装/替换包装。
普通组件需要复制时使用 auto copy = *component；已有普通组件通过 *component = copy 更新并通知。


add_component<T>(args...) 正常构造组件；已存在同类型组件返回 invalid_state，保留原值。
remove_component<T>() 缺失时返回 not_found。两者对失效对象返回 invalid_handle，
对错误线程返回 invalid_state。归属、层级和变换类型属于私有受控数据，禁止通过普通组件接口操作。
自定义组件在自己的实现单元包含私有 component_storage.hpp 并实例化对应访问函数，
不向调用方传播 EnTT。当前未提供任意类型的动态组件注册桥。

默认相机选择使用独立 camera_selection.hpp，不是 World 方法。
选择状态作为渲染模块定义的 World 级扩展状态保存；World 本身不认识相机类型或清理规则。
查询校验实体 generation 和相机组件，失效后返回 not_ready；删除组件时清除选择。

## ECS 与 System

EnTT `3.15.0` 只用于 World 的内部组件存储。Gneiss 公共头、Entity ID 和 C ABI 不依赖 EnTT。
内部 System Scheduler 按注册顺序串行执行，遇到第一个失败结果立即停止并保留该结果。

Camera 与 Mesh Renderer 是当前原生组件，其行为由
[Render 资源、组件与帧提取](render.md)定义。Transform 和 Camera 可通过
[Type Registry 与反射元数据](reflection.md)注册并按 World、Entity、Type GUID 和 Field ID 访问；
公共接口不暴露 EnTT 或组件地址。可持久化对象 UUID 仍由场景格式负责，不使用运行时实体 ID。

## World 到 Render 的变化同步

`function/scene/rendering/world_render_binding` 借用 World 与 Render，拥有一个持久渲染场景；
World 核心不持有绑定或 Render。绑定首次读取已有网格对象，之后消费 World 的合并变化记录。
局部变换与改父节点通知受影响子树；对象销毁保留带 generation 的删除身份，不借用已销毁组件。
组件移除后重新添加保留移除事实，绑定为其创建新的渲染实例身份。
每个消费者拥有独立记录，成功同步才清空；无消费者时不积累历史。
具体生命周期及使用见 [Render](render.md#world-增量绑定)。
