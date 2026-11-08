<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景加载、实例、序列化与卸载

## 运行期身份与所有权

`scene_instances` 拥有 `scene_instance`，对外使用非拥有的 `scene_instance_id`。
节点统一使用 `scene_node_id`，校验所属域和槽位代次；不能跨 World 使用，也不保活对象。
持久化身份使用 UUID，文件不保存运行期 ID。

World 拥有场景树和 ECS；场景实例记录自己的全部节点并负责统一删除。
ECS 组件持有 mesh/material 等引擎资源强引用，不保存第三方后端对象。
`prefab_binding` 保存模板资源、实例 URI/覆盖与来源 UUID 到实际节点的非拥有映射，借用 World 和资源服务。
它不在析构时销毁节点，也不保留创建期间的有效描述副本；投影由无状态的 `prepare_prefab_description` 处理。

## 原生结果接口

`scene_instances` 的创建、查询和捕获接口返回 `result<T>`，失败时只返回错误，不提供部分输出。
原地修改、删除和卸载继续返回 `error`。调用方先检查结果，再取得值：

```cpp
auto created = scenes.create_empty(scene_uuid);
if (!created)
  return created.error();
auto captured = scenes.capture_description(*created);
if (!captured)
  return captured.error();
// *captured 是自有场景描述，可用于后续编码。
```

`capture_prepared` 返回 `result<scene_capture>`，将实例化描述与资源绑定一起返回；
`capture_prefab_header` 返回 `result<prefab_header_snapshot>`，包含对象描述与 Prefab 强引用。
`get_node_info` 返回的 `scene_node_info` 仍包含借用字段，结果包装不延长场景寿命。
单实例与集合层共用值结果接口，集合层只校验线程、句柄并转发；不再构造输出参数再包装结果。
集合管理实现在 `scene_instances.cpp`，单实例实现在 `scene_instance.cpp`，
编辑和 Prefab 操作分别位于对应 `scene_instance_*` 文件。

## 编辑入口边界

`scene_document` 持有不可变编辑版本。属性、组件增删、层级、子树和 Prefab 操作先生成领域候选，
通过校验后接受版本，再更新 World。返回成功表示编辑内容已接受；`projection_status()` 单独报告同步状态。
同步失败保留文档，保存及后续合法编辑不依赖投影成功。`synchronize()` 可重试完整投影，成功后须重新查询节点 ID。
完整重建通过 `scene_instances::replace_prepared` 保留场景实例句柄，成功才替换旧投影。

`capture_content()`、`serialize()`、保存和子树快照读取文档内容；打开既有实例的 `adopt` 仅在接管时
捕获一次初始描述。直接修改 World 不进入文档。`capture_nodes()` 根据文档内容及 Prefab 回放生成自有查询结果，
仅从运行时关联节点/实体 ID；尚未创建或已被底层删除的节点没有有效运行时 ID。
`find_node`、Prefab 查询及选择查询同样返回自有结果，不能保留临时查询结果内部的指针。

网格绑定的历史保活存放在文档版本的资源引用中，不是第二个加载缓存。
`replace_mesh_binding` 先校验文档绑定是否匹配 expected，再修改描述和资源引用。
文档关闭或 Application 停止后拒绝操作；跨线程调用不改变文档。
当前宿主仍通过 `editor_command_history` 持有局部/子树和整文档历史，统一前后版本历史尚待迁移；
文档内部的版本状态暂不重复记录另一份撤销栈。

Prefab 编辑开始时从组件覆盖快照取得原资源引用；异步加载完成后的冲突检查继续执行。
网格/材质 URI 覆盖由 `prepare_mesh_renderer_overrides` 统一计算，保留其他覆盖记录，
恢复来源值时删除对应稀疏覆盖。该函数不应用资源、不写历史、不改变输入；成功后由调用方提交。

## 准备阶段类型

- `scene_preparation_inputs`：已加载的 Scene/Prefab 不可变描述引用及读取统计。
- `prefab_instantiation_data`：Prefab 描述引用、父节点优先顺序与层级深度，不是文件格式描述。
- `scene_instantiation_data`：完成排序与依赖收集的实例化输入。
- `scene_instances::resource_bindings`：URI 到运行资源强引用的映射，不是 CPU 资产加载缓存。

这些名称替换旧名称，不增加新状态或兼容别名。

## 加载阶段

文件读取和依赖准备由异步加载协调器完成，见[调度指南](../guides/task-scheduling.md)。
`scene_instances::instantiate_prepared` 只消费已准备的描述和资源引用，在 World 所属线程同步创建：

1. 按唯一 `objects` 列表计算父节点优先顺序。
2. 普通对象直接创建节点、实体和组件。
3. 带 `prefab` 的对象以模板根创建实例根，再创建模板子节点和应用覆盖。
4. 全部成功后返回场景实例 ID；失败回收本次创建的节点及 ECS 组件。

同步创建阶段不跨帧，也不支持中途取消；文件和资产准备阶段可取消。
`create_empty` 创建无节点场景，不读取文件。`capture_prepared` 捕获内存描述及实际绑定的资源引用，
可供编辑器恢复使用；它不读取文件，也不创建隐藏的备份实例。

## 对象与来源查询

`get_node_count`、`get_node_info` 枚举所有实际节点，返回 `scene_node_info`。
成员显式标记是否参与保存，普通对象、实例根与模板展开子节点可以交错排列。
不能依赖实例根的枚举位置或节点连续排列，也不能跨修改保留枚举下标。查询值包含节点、父节点、实体、变换、组件信息和可选 `prefab` 来源信息。
字符串视图仅借用到下一次场景修改或所属对象失效；跨修改保存时必须复制。

`find_node` 按显式对象 UUID 查找普通对象或 Prefab 根；模板子节点通过来源信息中的
实例 UUID 与来源 UUID 区分。同一模板的两个实例拥有不同节点和实体，可以共享模板资源。

## 编辑和保存

运行时 World 的节点身份、层级、局部变换与 ECS 组件描述实际运行状态；编辑内容以 Editor 文档为准。
`scene_instance` 只长期保存场景 UUID、成员标记和 Prefab 绑定，不维护完整 `scene_description`。
描述仅用于加载输入、捕获输出和撤销快照。`scene_object.hpp` 委托 Function/Scene 的
`component_adapter.hpp` 按描述 GUID 安装与捕获组件，内建适配相机与网格渲染组件。
扩展组件注册 `component_operations<T>`，提供验证、安装、捕获、删除、比较操作；
属性覆盖默认使用 Meta，资源依赖枚举默认为空。Meta 仍只负责描述数据的反射与编码。

注册须在首次组件查询之前完成；重复 GUID 返回 `invalid_argument`，冻结后注册返回
`invalid_state`，缺少适配的描述返回 `unsupported`，不会在实例化或保存时静默丢弃。
运行组件回调只在 World 所属线程调用；验证、比较、描述编辑与依赖枚举须可并发调用。
安装不得读取文件或阻塞等待，失败不得改变目标组件，并须能用捕获描述恢复旧值。

实际组件仍存入 World 唯一 ECS。需要直接访问 ECS 的适配实现使用内部
`world_component_access.hpp`，其借用指针只在所属线程及 World 生存期有效；
普通 Scene/资源接口不暴露 EnTT。节点销毁依靠 ECS 正常析构组件，不另建组件副本表。
通用扩展示例见 `tests/support/movement_component.hpp`。编辑器通过
`scene_document::components` 恢复文档中的有效组件描述，用只读 `component_description_base::view()` 和
`meta::type` 绘制通用属性控件；视图不超过描述寿命，不借用 ECS 成员地址。

通用组件编辑在文档描述的独立候选上修改并校验，接受新版本后调用 Scene 的投影入口。
文档为 Prefab 来源对象生成稀疏覆盖，恢复来源会移除覆盖；投影入口对所有对象只写入有效 ECS 数据，不再生成覆盖。
历史保存描述快照，通过对象 UUID 或实例/来源 UUID 重新定位，支持撤销、重做和连续修改合并。
节点/组件不存在时返回错误，不重建组件。Transform、主相机只读规则和资源 URI 异步加载继续使用
现有语义；通用注册不等于新增组件、删除组件或容器结构编辑能力。

当前通用面板支持已有反射控件的对象、序列元素和标量（含枚举）。Prefab 覆盖不支持改变序列长度、
关联容器或 optional 状态；不支持的修改在提交前拒绝。多字段覆盖在同一副本上应用后才验证整体。

保存只收集显式登记的持久化成员；Prefab 展开子节点通过源引用与覆盖表达。
直接在 World 创建、挂到场景下的临时对象不会自动写入文件。持久化成员若被底层挂到
临时或其他场景父节点，捕获返回 `invalid_state`，不生成悬空父引用。

运行时资源替换接口校验输入绑定；编辑器文档可接受结构合法但视觉依赖尚未就绪的描述。
文档保留 URI 和资源身份，投影失败通过 projection_status 报告，不撤销已接受的内容。
直接修改 World 不改变文档的保存内容。

Editor Document 保存及完整子树撤销读取文档版本，不反向捕获 World。
网格创建输入命名为 `mesh_node_creation`，与撤销快照区分；显式的运行时属性应用操作才能改变文档。


`game_object_description` 只有一份身份、名称、父关系和变换；可选 `prefab` 保存 URI 与相对覆盖。
没有独立的实例描述数组。根名称与变换采用外层对象值，根组件来自模板与实例覆盖。
具体格式与校验见[场景格式](scene-format.md)。

`set_node_name` 支持普通对象与实例根。`reparent_node` 支持显式对象，普通自有对象可挂在实例根下，
不能挂到模板内部节点。模板内部结构暂不提供增删或重排覆盖。
`destroy_node` 对所有显式对象统一要求没有实际子节点；完整删除使用 `destroy_subtree`，不按 Prefab 类型切换删除语义。
`set_node_transform` 及组件增删、替换接口只修改运行时有效数据，不生成 Prefab 覆盖。
覆盖生成及恢复属于 `scene_document`。运行时临时修改不会更新加载时的 Prefab 引用；重新实例化仍采用文档覆盖。
当前运行实例仍保留加载时的来源映射和覆盖记录，待 PLAN-105 后续迁移。

`capture_description` 采集显式对象，`serialize` 编码为当前格式；来源生成节点不重复输出。
未知 JSON 扩展字段按对象 UUID 对齐保留，不按旧数组下标绑定。
`capture_subtree`、`restore_subtree` 使用统一的 `scene_subtree_snapshot`，支持普通父对象包含 Prefab、
实例根包含自有对象及多个同源实例。快照以不可变 `scene_record` 保存显式对象，以 `prefab_record` 保存当时使用的 Prefab 源内容，另存外部父 UUID 和实际资源强引用；
来源子节点由模板与覆盖重建，不重复展开成持久化对象。资源按显式对象 UUID 关联，避免同 URI 的不同版本混用。

捕获完成后才删除。撤销保留显式 UUID，节点与实体 ID 重新生成；复制为所有显式对象生成新 UUID，
通过 `remap_scene_record` 修正内部父引用及组件/覆盖值中的对象引用，保留模板内部来源 UUID。
引用只有同时匹配源文档和复制对象才改写；跨文档、子树外引用及资产身份保持原值，普通 GUID 字段不改写。
重映射遍历 record、sequence、map、optional 和 variant；拒绝缺失、重复或 nil 的映射。
此入口不检查外部引用目标是否存在，组件实例身份重映射仍待接入。
恢复只消费内存快照，不查询资源缓存或读取文件。
`description()` 和 `prefab_source()` 从冻结内容创建独立描述；修改恢复后的描述不会修改历史。
文档级快照同样冻结场景和 Prefab 内容，刷新前的并发编辑检查比较内容，不依赖源对象地址或 JSON 文本。
`resource/scene/scene_record.hpp` 中的对象记录显式保存 UUID、父 UUID、名称、变换及 Prefab 引用；
组件记录保存类型 GUID 和反射冻结的属性 value，当前尚无组件实例 UUID。
恢复创建独立组件描述并检查类型、重复组件、Prefab 覆盖与场景层级；内存构造记录同样执行校验。
复制版本共享只读内容，资源保活仍由所属历史显式持有，记录本身不持有 World 或可写组件引用。
恢复失败清理本次创建的节点，原场景与快照保留；新节点仅在构造成功后并入所属场景成员集合。

普通子树历史与 Prefab 放置／删除历史使用同一快照和恢复入口；不再维护独立的 Prefab 放置快照结构。
如果历史之外新增了对象，旧放置命令的撤销会返回 `invalid_state`，避免删除快照无法恢复的对象。
`destroy_subtree` 统一清理节点、来源关系与 ECS 组件。模板内部结构编辑的限制保持不变。

## 来源重载与显式刷新

首次加载与结构重载只要求描述有效和渲染资源身份有效，不等待视觉内容成功。
结构完成后使用独立视觉队列逐项加载；单项失败不阻断后续项，也不撤销场景结构。
结构回执与视觉完成通知分离，后者保留资源 URI、错误和依赖诊断，并报告到 `scene.assets`。
缺失网格暂不绘制，缺失材质使用默认值；首次材质加载缺失纹理时使用对应槽位默认贴图，
但加载请求仍返回依赖错误。已有资源更新失败保留原有可用内容。
场景描述解析、层级和 Prefab 描述准备失败仍中止结构操作；本规则不包含缺失 Prefab 或未知组件占位。


`reload_loaded` 应用已加载场景描述；当前要求 Prefab 引用声明保持一致，来源变更另走
`reload_prefab_loaded`。来源重载按 UUID 匹配节点，组件差异以当前 ECS 捕获值为准，目标值来自新描述及覆盖；保留匹配节点身份、实例根值和合法覆盖。
原地重载不允许更换模板根 UUID；失去目标的覆盖返回错误，不静默绑定到其他节点。

显式刷新通过 `prefab_placement` 准备来源快照，再调用 `instantiate_prefab_ready` 替换。
先删除旧实例，失败不恢复旧实例，也不保证同源多个实例一起成功。
挂在根下的自有对象会暂时移到原实例父节点，刷新成功再挂回新根，保持身份及局部变换；
刷新失败时仍保留在原实例父节点下，不随旧模板销毁。
编辑器历史保存变化对象及变化资源项的前后数据、轻量版本标识，撤销／重做更新运行投影，不保留隐藏实例或刷新令牌。
刷新、撤销或重做后必须重新查询节点 ID 与字符串视图。

## 卸载

`unload` 删除场景全部节点及实体并释放来源引用；场景 ID 立即失效，重复卸载返回句柄错误。
运行时先清理场景实例，再销毁其借用的 World 和资源服务。
GPU 资源实际回收遵守资源服务及渲染后端的延迟销毁规则，不由场景直接释放后端句柄。

## 普通对象组件增删

`component_editor_types()` 提供注册组件的添加/删除能力，显示名称来自 Meta。
组件通过显式 `create_default` 工厂提供有效默认描述，`removable` 选择是否允许交互删除；
不会自动把 `T{}` 当作有效组件。相机提供默认描述，网格仍需用户选择资源。

`add_component_description` 向对象添加缺失组件，重复添加返回 `invalid_state`；
`remove_component_description` 删除缺失组件返回 `not_found`。编辑器文档对 Prefab 根及来源节点记录组件结构覆盖，
格式及冲突规则见[场景格式](scene-format.md#prefab-组件结构覆盖)。已有组件替换仍使用 `set_component_description`。

编辑器组件增删通过稳定 UUID 重新定位目标，统一记录前后文档版本。撤销恢复组件实例 UUID；
直接删除后重新添加则产生新 UUID。历史拒绝过期文档基线，不覆盖历史之外的编辑。网格历史保留实际资源强引用，
不通过 URI 重新加载来恢复旧资源。新资源组件需要相应资产命令，通用同步增删历史不会接管资源准备。
实施记录见 [PLAN-085](../plans/PLAN-085-component-lifecycle-editor.md)。


Prefab 组件历史使用 `scene_document::capture_component_override` / `scene_document::set_component_override` 保存、恢复单个组件覆盖，
包含网格资源强引用。面板支持组件添加、移除及 Restore Source；网格恢复来源先通过资产入口异步准备资源。
同步 Scene 接口不读取文件；资源缺失返回未就绪，调用方准备成功后再提交。
来源重载先验证受影响实例的完整覆盖，冲突不修改实例及其记录；`reload_prefab_loaded` 可输出路径诊断。
诊断包含来源对象 UUID、组件 GUID 和失效属性路径。
Apply/Unpack 与实例化、Reload 统一通过 `prepare_prefab_reference` 生成有效描述，支持组件增删和属性覆盖。
Apply 在生成文件写入计划前，使用新的来源描述检查当前编辑场景所有同源实例；其他实例的覆盖保持不变，
冲突诊断包含实例 UUID 和来源对象、组件及属性路径。检查不扫描项目其他场景。来源保存成功后清空普通撤销历史，
不提供磁盘撤销或多文件事务。
Unpack 将有效描述物化为普通对象，移除来源关联，使用新的 UUID 并映射父子关系；保留实例根的名称、变换、
父关系和自有子对象。支持快照撤销/重做，场景在后续保存时落盘。
两种操作均使用类型化 codec 保存，未识别的 JSON 字段不会保留。实施见 [PLAN-087](../plans/PLAN-087-prefab-apply-unpack.md)。

### 编辑器覆盖列表与恢复

Inspector 的 Instance Overrides 从当前实例的属性覆盖与组件结构覆盖临时生成，显示对象、组件、
嵌套属性名称或容器下标，并区分 Modified、Added、Removed。Locate 定位来源对象；失效对象保留 GUID。
Restore 按稳定键移除单项记录，Restore All Overrides 清空整个实例的覆盖；二者不改来源文件，
不清除实例名称、放置变换或自有子对象。新增组件以整条新增记录恢复。

恢复复用场景快照及命令历史，支持撤销/重做和正常场景保存。缺少来源渲染依赖时预留资源身份并异步加载，
采用合法描述不等待视觉内容；加载失败保留描述及资源 URI。快照持有资源强引用，撤销无需读盘。
Apply 冲突显示可读实例/对象/组件/属性名称和原始路径；Locate Prefab Conflict 定位目标，
目标对象不存在时退到实例根。其他无对象路径的错误仍显示原始诊断。

恢复的候选准备与历史提交由 `scene_document_edit` 承担，位于编辑器文档模块；宿主只发起并推进操作。
`begin` 捕获快照和内容基线，并为缺失依赖预留资源强引用；`advance` 检查文档身份、历史占用
和当前内容基线，随后同步提交一条历史，不等待资产加载组。
取消释放未采用的候选引用，终态重复推进不重复提交。调用方继续负责推进应用 I/O。
文档及应用须覆盖操作和历史的寿命；采用快照后节点句柄可能变化，调用方按稳定作者身份重新定位。

文档恢复的内容冲突在提交前报告，不通过 JSON 比较整场景。
候选采用前比较文档内容与冻结 Prefab 来源，不反向读取 World；修改后恢复相同内容可继续采用候选。
这与撤销历史的版本身份检查不同：历史要求完全相同的版本基线。场景切换、关闭、取消和历史占用
仍在每次推进时检查。早期内容比较方案见[审计与取舍](../plans/PLAN-099-document-conflict-validation.md)。

恢复入口范围如下：

| 入口 | 恢复范围 |
| --- | --- |
| 属性旁 Restore Source | 通过既有属性编辑写回来源值，清除对应属性覆盖，其他属性不变 |
| 覆盖列表 Restore | 按稳定键移除一条属性或组件结构记录 |
| 组件 Restore Source | 移除该来源对象、组件的全部结构及属性覆盖 |
| Restore All Transform | 组件范围恢复的 Transform 快捷入口 |
| Restore All Overrides | 移除整个实例的所有覆盖 |

组件、Transform 和实例恢复共用文档操作；旧网格恢复接口转发同一操作，不维护独立的恢复加载流程。
属性编辑仍复用既有反射命令及资源 URI 编辑入口，无须另建一套属性恢复状态。

### 组件身份与编辑策略

`component_description_type` 通过组件适配器的 `type/property_type` 映射识别描述身份，未注册身份原样返回。
Prefab 覆盖捕获、恢复及有效描述准备共用此查询，相机身份映射只在内建适配器声明。

组件注册可声明 `editor_route`（当前为普通反射编辑或网格资产编辑）及 `prepare_editor` 回调。
通用属性命令负责反射修改、描述校验和历史；提交和历史重放前调用适配器策略并再次校验。
相机策略保留当前主相机状态，避免撤销其他相机参数时意外切换主相机。Transform 继续使用场景树变换入口。
此路由仅表达当前已实现的编辑入口，不代表任意第三方资源组件已有通用异步编辑支持。

`meta::property_flags::editor_read_only` 禁止普通属性面板及属性路径命令编辑，独立于序列化的 `read_only`。
相机 `is_primary` 使用该标记，仍正常编码/解码；主相机切换使用显式操作。
自动收集组件属性覆盖时不将此类字段的运行状态差异写为覆盖。适配器负责整体对象采用时的运行状态保护。

### 编辑资源准备

普通网格资源选择与 Prefab 操作统一调用 `application::prepare_assets`。
宿主私有 resource_load_group 直接请求 asset_manager；场景视觉加载使用独立请求组，
两组共享同一资产缓存，不共享回执槽位。场景服务不提供通用资源转发入口。

行为如下：

- 所有资源已就绪时直接返回 `applied` 和强引用，`request` 为零，不占加载槽位。
- 存在缺失项时返回 `preparing` 和请求编号；加载组保留已就绪引用，仅加载缺失项。
  通过 Application 推进 I/O 并轮询完成值；完成值包含全部资源，按来源匹配，不依赖输入顺序。
- 场景加载、视觉加载、重载或旧域回收期间拒绝新增异步准备；只查询就绪资源不覆盖已有请求。
- 显式 `reload=true` 忽略就绪缓存；Prefab 刷新使用此方式。
- 入口限宿主线程，关闭后拒绝调用。失败及取消不提交编辑器文档，旧绑定与历史保持不变。

普通资源选择中 URI 未改变的部分直接保留旧强引用，不重新查询缓存。弱缓存清理由应用正常资源
推进承担，命令完成时只释放自身引用。文档不维护额外资源缓存。

文档恢复 `scene_document_edit` 直接通过资产管理器预留缺失资源身份，不占用上述加载组槽位；快照原有引用保持不变。
`documents/scene_document_snapshot.hpp` 只提供内容及纯候选计算：
`make_scene_document_snapshot` 冻结已准备描述，`make_scene_document_inputs` 恢复独立领域输入，
`prepare_scene_document` 计算修改和缺失依赖。这些调用不需要 Application、World 或历史执行器。
宿主捕获、运行时恢复及异步历史执行位于 `documents/scene_document_edit.hpp`。
`capture_scene_document` 读取文档版本；`restore_scene_document` 接受内容后尝试同步，失败不反向回滚已接受的编辑。

`capture_scene_document` 返回 `result<scene_document_snapshot>`；`prepare_scene_document` 返回
`result<prepared_scene_document>`，其中包含 `snapshot` 和 `missing`。同步操作在 `missing`
非空时先预留资源身份，再提交历史，不要求内容加载完成。

`poll_prepared_assets(request)` 限所属线程，仅推进对应资源请求，不驱动文件 I/O；未完成返回
`not_ready`，不存在或过期编号返回 `invalid_handle`。`discard_prepared_assets(request)` 释放
该请求的观察权及回执槽位，关闭期间也可清理，绝不影响其他编号；底层在途 I/O 由资源服务回收。
网格编辑和 Prefab 编辑按编号领取结果，不消费其他编号的回执；文档恢复不占用这些编号。
网格及 Prefab 的资源阶段取消直接放弃请求、结束该阶段，返回 `cancelled`；文档关闭或应用停止
则清理请求并返回 `invalid_state`。Prefab 文件准备仍可取消；版本历史同步切换内容，不创建异步历史请求。
Application 负责推进 I/O，编辑命令通过既有 `advance_scene_load` 推进入口后再领取对应结果。
宿主的 `cancel_assets(request)` 仍保留取消回执；不传编号表示取消当前请求。

### Runtime 资产重载请求归属

Runtime 宿主将 IPC 会话/修订关联到 `request_assets` 返回的实际编号。轮询按该编号领取结果，
取消同时匹配会话、修订和编号；进度只报告所属请求，结构重载不取消其他资源请求。
完成、轮询失败及退出时释放编号。失败或过期请求不更新已应用修订，后续可重试相同修订。

重载协调器把轮询错误或异常转换为一次失败回执并结束等待；`not_ready` 继续等待，
成功结果才推进已应用修订。保留逐项生效语义：部分成功不回滚，失败项保持原内容。
实施及验证限制见 [PLAN-098](../plans/PLAN-098-runtime-reload-request-ownership.md)。

## 场景服务请求结果

scene_service 不持有执行器；资产管理器负责准备任务调度。configure 只接入用于内存统计的
Render 与场景进度通知。关闭仅释放场景的请求观察者，不停止资产管理器。

- request_scene 返回 result<uint64_t> 请求编号；activate 返回 result<scene_load_completion>。
- poll_scene 返回 result<optional<scene_load_completion>>；poll_reload 返回
  result<optional<scene_reload_completion>>。
- 外层错误表示调用失败；成功但 optional 为空表示暂无回执；有值表示终态，工作成功与否由
  完成值的 result 表示。完成回执领取一次，失败回执同样会被消费。
- Application 保留既有宿主输出参数适配；编辑器与 IPC 无需了解场景服务的内部请求组。


### 场景版本历史

属性、组件生命周期、对象层级、子树、Prefab 放置/删除/刷新及运行属性采用
`execute_scene_edit`；Gizmo 连续预览由 `record_scene_edit` 收尾。统一历史只保留前后版本和
稳定选择身份，不再持有各领域的 undo/redo 执行回调。组件原地更新在重放时重新执行编辑适配，保留当前主相机等运行策略。

撤销必须从记录的 after 版本切回 before，重做反之。中间发生未记录的独立编辑时返回
`invalid_state`，保留当前内容及历史游标；即使内容后来改回相同值也不冒充原版本。
先恢复已知版本或清理旧历史后才能继续。这条规则避免整文档历史覆盖不属于它的修改。

相邻同合并键且版本连续的编辑可合并；保存点阻止跨点合并，新编辑清除 redo 分支，历史保留既有条数限制。
文档必须比历史活得更久；关闭/切换文档时所属编辑会话清理历史。清理后不再持有历史专有资源引用。

切换版本后通过 `project_scene_document` 更新运行投影。已有成功同步基线，且对象集合和资源身份不变时，
Transform、组件以及显式对象名称和父关系差异原地更新，node/entity 保持。
父关系批量调整先脱离旧父节点再连接新父节点，避免旧树导致临时循环。其他变化或缺少同步基线
仍完整重建；scene_instance_id 保持，node/entity 可能改变，调用方按文档身份重新定位。
同步失败仍完成文档撤销/重做，通过 `projection_status()` 报告，并可重试同步；保存读取文档。
对象增删与 Prefab 内部结构增量更新尚未迁移；不等待纹理或网格 GPU 上传完成。
局部历史的目标与阶段见 [PLAN-106](../plans/PLAN-106-document-history-projection.md)。

属性编辑仍按路径计算 Prefab 差异，保留与本次修改无关的冲突；来源暂时缺失的覆盖不会被无关编辑清除。

### 文档对象引用诊断

`scene_document_snapshot::reference_issues()` 检查当前文档及冻结 Prefab 来源，底层纯函数
`inspect_object_references` 也可接收调用方提供的外部对象目录。检查递归覆盖记录、序列、
map 键/值、optional、variant、组件结构覆盖及属性操作值，不读取文件、不创建 World 对象。

诊断包含来源文档、对象、组件 UUID 和目标引用。双 nil 表示空引用；只有一侧 nil 为
`invalid_argument`；已知文档缺少目标为 `not_found`；未提供的外部文档为 `not_ready`。
诊断不删除原引用，也不阻止编辑、删除或保存。这里提供查询接口，不自动扫描整个项目，
也不把未知外部文档误报成确定丢失。


### 组件候选隔离

`modify_component`、`edit_component`、`edit_runtime_component` 在执行修改回调前按反射内容创建独立候选。
内嵌引用不再因 C++ 浅拷贝与来源共享，回调返回失败不会写坏来源内容。
描述需要支持内存捕获/恢复；不能恢复的类型返回错误，不退回浅拷贝。这些入口不执行资产 I/O，
需要外部引用解析而未提供解析能力的描述仍会返回错误。


### Prefab 编辑候选

`prepare_prefab_component_edit` 接收 Prefab 来源描述、当前引用描述、来源对象 UUID、当前组件和替换组件，
返回新的引用描述及冲突列表。函数不访问 World、文件、资源服务或编辑历史；当前组件与替换组件类型必须一致。
来源组件生成属性差异，已有本地新增组件更新其整项描述；组件增删仍使用单独入口，禁止通过属性编辑隐式恢复已移除组件。

本次触及路径由当前组件与替换组件的差异确定；来源与替换组件的差异形成持久覆盖。
重复修改不会累积同一路径记录，恢复来源值移除差异；未触及的失效覆盖及其他对象覆盖保留。
输入不变，失败不返回部分候选。`scene_document` 使用这个入口更新文档覆盖，
随后向运行层下发有效组件。运行时 `set_component_description` 不调用覆盖生成入口。

`prepare_prefab_transform_edit` 接收来源节点的当前及替换变换，与组件入口共用反射差异计算、
重复覆盖归并和未触及冲突保留规则，返回 `prefab_edit_candidate`。非法变换拒绝接受；
来源根节点返回 `invalid_argument`，实例根的局部变换由场景对象管理。
`scene_document::set_local_transform` 采用该候选更新文档，再通过 `set_node_transform` 投影运行时变换。

`prepare_prefab_component_override` 计算组件添加、移除或恢复来源的完整覆盖候选。
空 added 且 removed=false 表示清除结构覆盖，可携带该组件的属性覆盖；结构覆盖和属性覆盖不能混用。
仅本次目标的冲突导致失败，其他目标的冲突保留。`prefab_edit_candidate` 同时包含回放后的有效描述，
运行时安装直接使用该描述，不再次回放。此入口不加载资源、不访问 World。

### 编辑文档的纯内存组合

`compose_scene_document` 从 `scene_document_snapshot` 及其冻结 Prefab 来源生成临时结果，
不读取文件、不访问 World。`document_object_address` 以 object 表示显式对象 UUID，
source 非空时表示该 Prefab 实例内的来源对象；父地址使用相同规则。
同源的两个实例因此不会共享对象地址，组件实例 UUID 仍保持来源身份。

组合结果包含有效组件、实例根名称与变换、来源变换、覆盖键，以及按实例 UUID 归属的冲突。
失效覆盖保留在输入文档中并返回诊断；缺少必需的冻结来源返回 `not_found`。
编辑目标定位和层级查询共用该入口，结果不写入历史、不成为另一份权威状态。
当前节点句柄仍由运行实例身份查询匹配；独立投影关联尚待 PLAN-105 后续迁移。

### 局部文档历史记录

`document_patch_command` 保存变化对象、Prefab 来源项和资源项，不保留整份前后文档。
`revision` 只保活所属文档及版本身份标记，旧版本内容可释放；保存点按版本标识比较。
正反应用先检查文档身份、旧值和对象顺序，在独立候选中应用并校验，失败不改变原文档。
连续同键操作合并最初旧值与最终新值，撤销恢复通过同一投影入口更新 World。
存活且顺序不变的对象按名称、父关系、变换、组件集合与 Prefab 引用分别保留变化字段；
重命名不会保活该对象的无关组件和来源引用。组件内部属性路径细分、对象增删投影及编辑会话拆分
尚在 PLAN-106 中推进。
