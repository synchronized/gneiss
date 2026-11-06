<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 资产 URI、目录挂载与缓存

普通资源、网格和纹理统一通过[文件服务](file-service.md)异步读取；旧 VFS 和同步加载入口已删除。

## 值返回入口

`asset_manager::load<T>(uri, reload)` 返回 `result<asset_request<T>>`，表示是否成功提交请求；
并不代表异步解码已完成。`find_loaded<T>(uri)` 与 `store<T>(uri, value)` 返回
`result<asset_ref<const T>>`。网格和纹理也使用同一个 `load<T>` 与请求类型。

Scene、Prefab、Texture、Mesh、Material 资产对象均由同一个 `asset_manager` 加载。
`create_mesh/create_texture/create_material` 返回对应资源引用的 result；`create<T>(value)`
创建不登记 URI 的不可变 CPU 资产。未绑定 Render 时可加载 Scene/Prefab/ActionMap 等非渲染资产，渲染资源创建/加载返回 `invalid_state`。
公开 `load/find_loaded/store<T>` 仅接受 asset 派生类型，普通 CPU 值与描述返回 unsupported。
`store<T>` 不允许外部替换渲染资源拥有者。内部描述 Decoder 不受此限制。
内存资产解码返回 `result<decoded_asset, asset_diagnostic>`。判断与取值规则见 [Core](core.md#错误状态与值结果)。

`asset_request<T>::take()` 返回 `result<asset_ref<const T>, asset_diagnostic>`。
调用不等待：not_ready 和其他失败不消费请求，成功后请求失效，重复领取返回 invalid_state。
成功后通过请求的 `metrics()` 查询来源快照、解码字节估算、解码与队列耗时，
`source_bytes()` 返回读取字节总数。已领取引用不受取消或管理器关闭影响。

`load<T>(uri, asset_load_options)` 可指定 reload、texture_profile。
`find_loaded<T>` 与 `store<T>` 也支持相同 options；查询和采用须匹配加载参数。
所有类型统一使用弱缓存，无缓存策略参数。内置网格/纹理使用专用 Loader，
不能用单缓冲区 register_decoder 覆盖；其他类型先用注册解码器，否则使用反射解码。

`asset_manager::reserve_mesh/texture/material()` 返回对应资源引用的 result，
`reserve_resource(source)` 返回 `result<render_resource_ref>`；它们只预留资源身份，不代表内容已加载。
`asset_manager::identify(uri, type)` 返回 `result<asset_identity>`，只解析来源身份，不读取资源内容。

## URI 规则

现有资产加载入口当前只支持规范形式的 `asset://` URI，例如：

```text
asset://models/triangle.mesh
asset://纹理/石头.png
```

URI 使用 UTF-8 和正斜杠，scheme 区分大小写。空路径、空路径段、`.`、`..`、控制字符、反斜杠、
冒号、查询、片段和百分号编码均返回 `engine::error::invalid_argument`。原生校验函数不访问文件系统：

```cpp
#include "engine/resource/asset_uri.hpp"

auto result = gneiss::engine::validate_uri(uri);
```

## 文件服务与资产根目录

Application 将 `application_configuration::asset_root` 装配为 `asset://` 只读挂载。
桌面使用 libuv 原生后端；浏览器单斜线开头的绝对路径借用预加载到 Emscripten FS 的目录，
HTTP(S) 地址使用 Fetch URL 基址。借用目录不会在后端销毁时被卸载。
文件服务按最长前缀路由，后端只接收相对路径；读回退链和唯一写入后端的契约见文件服务参考。
资产根为空时不建立挂载；文件缺失通过异步结果报告。

## 缓存与生命周期

内部资源强引用统一使用 `engine::asset_ref<T>`，底层共享标准智能指针控制块。
复制引用保活同一拥有者，移动转移引用，`reset()` 放弃当前引用；最后一个强引用释放时才析构
资源或执行原控制块删除器。引用不会自行加载资源，也不表示 GPU 内容已就绪。

| 入口 | 强引用与访问 | 更新规则 |
| --- | --- | --- |
| `asset_manager::find_loaded<T>`、`asset_request<T>::take` | `asset_ref<const T>`，直接访问资产对象 | 成功重载替换当前缓存，旧引用继续保活旧数据 |
| Prefab 描述 | `prefab_asset_ref`，即 `asset_ref<const prefab_description>` | 直接保活描述快照；重载不会修改旧描述 |
| 资产管理器、场景绑定、编辑器历史 | `mesh`、`material`、`texture` 强引用；混合批次用 `render_resource_ref` 持有类型化引用 | CPU 内容采用即完成，资源身份保持稳定，GPU 更新由 Render 独立处理 |

渲染引用通过 `resource_rid`、`resource_kind`、`resource_uri` 访问 RID、类型和 URI；空引用分别返回
零 RID、invalid 类型和空 URI。场景绑定校验所属服务及来源域，ECS 保存类型化资源强引用。
上传任务和在途渲染帧对具体数据的共享所有权不等同于资产身份引用。

类型化运行对象分别定义在 `resource/mesh.hpp`、
`resource/texture.hpp` 和 `resource/material.hpp`。
上层包含所需资源头，保存 `ref<const mesh>`、`ref<const texture>`、`ref<const material>`；材质持有纹理强引用。
资源对象析构只处理 CPU 状态与依赖，不调用 Render。管理器的延迟销毁队列先在所属线程
释放 RID，再析构资源对象；服务关闭后的晚释放只析构 CPU 对象。
CPU 数据表位于 `render_service` 私有实现，Loader 与宿主通过其 RID 接口操作。
功能层资产管理器负责强引用创建、元信息及材质依赖，详见 [Render 资源与服务边界](render.md)。


`asset_manager` 唯一持有最终资产弱索引，按「来源域 + URI + C++ 类型 + 加载参数」区分身份。
加载参数包含完整纹理 profile；reload 不参与身份。运行资源使用管理器绑定的 profile，
调用方传入不匹配的显式 profile 会被拒绝。自定义资产与内置资产共享索引，同 URI 的不同资产类型独立。
管理器只暴露 `cached_count()` 诊断计数，不暴露缓存修改入口。
类型键使用 `engine::type_id`，由同一 Engine 按规范 C++ 类型名集中分配，
只在当前运行期有效，不写入资产文件或公共 ABI；约束见 [Core 身份类型](core.md#内部基础身份类型)。
通用请求表只保留请求顺序和在途观察状态，不保存另一份当前资源。
通用异步加载默认复用成功缓存；显式 `reload=true` 才重新读取。
资源表统一通过 `resource_cache::load` 查询、按需执行回调并登记成功结果；
该入口返回 `result<asset_ref<const entry>>`，创建回调返回 `result<shared_ptr<const void>>`，
失败不会提供部分构造的缓存条目；显式重载失败
（包括回调抛异常或返回空资源）保留旧条目，首次失败不残留占位。相同键递归加载返回
`invalid_state`，不同键的依赖仍可递归加载。Prefab 普通加载和重载共用解析流程；
渲染预留在缓存未命中时才创建 RID，服务归属与 CPU 内容是否可用由资产管理器检查，完成不等待 GPU。
CPU 解码由注入的执行器运行，完成结果在所属线程通过同一入口登记；协作模式在所属线程执行解码。

请求取消只放弃当前观察者，不使其他引用失效。

统一资产索引只保存弱引用，不延长对象寿命。
请求结果与类型化引用直接共享实际对象的控制块，不再借用缓存包装的控制块；
最后一个引用释放后查询立即失效，collect_unused / release_unused 仅清理过期索引。
运行资源实际析构仍通过所属线程销毁队列完成，排队期间不会恢复弱缓存命中。
Application 只调用一次资产管理器推进和回收；场景运行层不再驱动另一套资源管理器。
CPU 缓存回收保留在途请求索引；CPU 重载任务暂时持有旧对象，
避免回收与失败重载叠加导致旧内容提前消失。
已完成但尚未领取的请求也保活结果。关闭通用管理器不使已领取的 CPU 描述引用失效。
失败不永久缓存，重载失败不替换仍被使用者持有的旧数据；
若旧数据已无使用者，重载任务结束后也会释放，不额外保留失败回退缓存。输出参数的失败行为以具体接口契约为准。

引用计数并不提供资源数据的线程安全。CPU 数据可按只读契约共享。运行渲染资源的最后释放可在
任意线程发生，删除器仅入队，由所属线程析构；服务关闭后走 detached 路径。
GPU 在途使用的安全回收由 Render 及后端负责，详见[运行资源生命周期](render.md#运行资源身份与销毁)。
缓存查询与资源采用限所属线程；异步文件服务与 CPU 解码执行器分别负责 I/O 和解码。

引用规则见 [ADR-077](../decisions/ADR-077-asset-references.md)，统一索引见
[ADR-078](../decisions/ADR-078-unified-asset-index.md)。

## 内部异步准备

网格使用 `decode_mesh_asset`，纹理使用 `decode_texture_asset` 接收已解析描述与负载；
材质通过 `adopt_material_asset` 共享已解码描述。各入口不读取文件、不递归加载依赖。
asset_manager 内部使用任务私有的 CPU 数据请求完成装配，不将这些中间负载登记到最终资产索引。
`mesh_data`、`texture_data`、`material_description` 的公开 `load/store/find_loaded` 返回
`unsupported`；工具需要原始数据时使用对应格式编解码，不建立第二份资产缓存。
内部材质解码仍支持显式 Decoder，供最终材质 Loader 使用。
私有 `detail/asset_loader.hpp` 的 Loader 消费字节并给出下一读取 URI：网格一次解码，
纹理先解码描述，再读取并解码所选负载。管理器只编排 I/O、执行器、请求合并与缓存登记。
成功领取共享只读 CPU 内容后，asset_manager 内部构造步骤直接交给 Render 共享准备入口，不复制大块负载。
取消只放弃当前观察者，最后一个观察者消失后取消后续工作，不等待工作线程。
内部 `decoded_asset::content` 使用 `std::variant`，只持有一种内容；`kind()` 从内容推导类型。
材质不重复保存参数与依赖 URI；内容采用前检查空引用、目标类型和来源身份。
文件服务返回字节后，加载编排层提交 CPU 任务；thread 与 cooperative 使用同一解码实现。
协作模式的单次 JSON/PNG 解码仍不可抢占，不承诺单帧时间上界。

准备结果拥有网格数组或纹理 CPU 内容，材质依赖以 URI 返回，由加载编排层继续请求。
纹理 Manifest 和所选负载摘要仍校验；CPU 负载常驻供 Render 重建，不保留可重新打开的文件来源。
文件更新由后续加载处理，不保证多个文件属于同一时刻。文件读取失败、取消和格式错误分别通过结果码报告。

## 结构准备与实例化边界

`asset_manager::prepare_scene(uri, reload)` 与 `prepare_prefab(uri, reload)` 返回 `asset_request<scene_instantiation_data>`。
它异步加载描述和结构依赖、执行层级排序并收集视觉资源清单，不创建 World、实体或场景节点，
也不等待视觉资源和 GPU。默认复用描述；显式 reload 重新读取。准备结果本身不建立第二份 URI 缓存。
同一 Prefab 描述可以由多个准备结果和实例共享。取消一个准备请求只放弃它的依赖观察权。

Runtime 的 `scene_service` 保留活动场景、同步实例化、激活、退场和视觉加载进度，
消费资产管理器的准备结果，不再自建解码任务 scope。结构可以先激活，视觉内容随后完成。
宿主依次分发文件完成、推进资产管理器，再推进场景与渲染；各层不嵌套驱动文件完成队列。

请求可以复制共享工作，取消和 `take()` 只影响当前观察者。管理器停止会终止在途请求，
等待自身解码任务退出；CPU 描述引用仍有效。关闭后运行资源引用不再具有可操作的后端 RID，
晚释放仅处理 CPU 对象，不访问已销毁的 Render。

## 场景请求的清理快照

场景取消停止后续 I/O/CPU 任务并清理尚未激活的逻辑对象。清理完成取决于请求与场景状态，
不等待普通资产的 GPU 上传回执。已成功采用的共享 CPU 资源不因取消而回滚。
活动场景或已提交帧持有的资源继续有效，GPU 上传及其保活由 Render 独立管理。

Runtime 的 `scene` 域消息可携带内部 `budget` 对象。容量字段为无符号 64 位字节数：
`candidate_logical_bytes`、`candidate_cpu_data_bytes`、`application_logical_bytes`、
`application_cpu_data_bytes`、`available_bytes`。
候选为已完成 CPU 批次统计，Application 为资源账本；均不是进程 RSS 或实际显存。

预算对象中的必填布尔字段 `cleanup_complete` 只有在失败/取消请求的内部请求与暂存状态清理后才为 true。
此时候选分项保留回收前的诊断值，Application 重新采样回收后的账本。
活动场景、共享缓存和旧渲染帧仍可合法持有资源；该字段不表示整个 Application 已清空。
成功激活将候选转移给活动场景，不使用这个标记表示释放。
预算对象缺少该字段或字段类型错误时拒绝消息，既有 IPC 域版本不变。

`cleanup_pending` 同样是必填布尔字段，不能与 `cleanup_complete` 同时为 true。
Runtime 的清理补报为事件，不重复发送请求响应；Editor 仅更新同一取消请求的预算。
生命周期依据见 [ADR-058](../decisions/ADR-058-scene-cancellation-cleanup.md)。

## 资源描述与运行期材质

`material_description` 是唯一的 CPU 材质描述，包含 `material_parameters parameters` 和
五槽纹理 URI；解析结果直接使用此类型，不再增加单字段文档包装。
材质仅读写 v6，字段及默认值见 [Render 资产格式](render-asset-formats.md)。
`material` 是稳定运行对象，保存 `material_parameters` 和五槽 `ref<const texture>` 强引用，
通过只读 `parameters()`、`textures()` 查询；它不再持有描述对象或纹理缓存条目。
后端 CPU 快照 `material_data` 保存同结构参数及纹理 RID。参数默认白色、不透明、
metallic=0、roughness=1；这不免除材质文件必填字段的校验。
运行期数据由渲染服务管理；后端 `material_mirror` 持有 Granit 对象。
描述不包含 RID、GPU 对象或编辑会话状态。可通过 register_builtin_resource_types 显式登记编解码能力，
接口见[反射参考](reflection.md#类型编解码)。

资源领域命名按职责区分：`*_description` 表示可持久化的描述，`*_parameters`/`*_settings`
表示参数，`*_data` 表示实际数据，`*_resource` 表示服务使用的运行期资源；
`*_document` 留给编辑器文档，`prepared_*` 与 `*_mirror` 表示准备数据及后端镜像。
此规则不要求数学值、网格数组等所有可序列化类型都使用 description 后缀，
也不将任务调度等其他领域的 description 解释为可持久化资源。

## 普通资源读取与解码

统一入口 `asset_manager` 位于 `engine/function/resource`，管理描述与运行资源的一份 URI 弱缓存。
宿主用 `configure(executor, profile, notification)` 一次配置异步加载；
仅需执行器时可用 `configure(executor)`，该重载保留已有 profile 与通知配置。
相同配置可重复调用；更换执行器、profile 或通知对象返回 invalid_state，不改变已有配置。
配置和停止请求状态只由管理器私有状态持有，渲染装配不保存配置副本。
缓存定义与渲染构造实现不进入宿主可见头清单。
纯 CPU 加载不要求 Render；程序化运行资源创建独立通过 `attach_render(render_service&)` 接入，
可以先于或晚于异步配置。同一 Render 可重复绑定，替换 Render 被拒绝。
`load<T>` 返回 `result<asset_request<T>>`，涵盖 Scene/Prefab/ActionMap 及纹理、网格、材质等资产对象。
缓存查询统一为 `find_loaded<T>(uri)`，混合查询使用 `find_loaded(render_resource_source, profile)`；
不读取文件、不等待 GPU，失败返回错误。类型化查询使用管理器配置的纹理 profile。
混合引用 `render_resource_ref` 直接保存三种类型化强引用之一，不暴露缓存条目。
`asset_request<render_resource_ref>::take()` 返回混合引用值，类型化请求返回对应资源强引用。
所属线程通过 `status()` 查询；成功表示 CPU 内容与依赖已采用。
同身份在途请求（含 reload）共享一次加载；请求复制共享观察权，cancel 仅释放当前观察权。
所有观察者释放后取消未采用工作；再次 reload 须在前次完成后调用，不建立版本覆盖协议。
材质依赖失败时材质请求失败，成功的独立资源不回滚。
Application 通过 `assets()` 暴露统一入口，分发文件完成后推进该管理器。
管理器停止并排空任务、场景释放资源后，再关闭 Render；迟到的资源引用释放不访问已关闭后端。
`collect_unused()` 回收过期弱缓存及待销毁后端资源，外部强引用仍保活对应对象。
内部加载文件位于 `detail/`，不向宿主开放。资源数据及格式编解码仍位于 `engine/resource`。

`render_resource.hpp` 声明 `render_resource_source` 及混合资源引用辅助函数；
场景批量进度和回执位于 Runtime 的 `resource_load_types.hpp`，属于 `gneiss::runtime`。render_resource_kind 当前只枚举 Mesh、Material、Texture，
自定义资产类型由 load<T> 的 C++ 类型确定。
`asset_construction_loading.cpp` 保存服务私有的单资源请求状态，`asset_decode` 只解码已有字节。
`detail::asset_construction` 定义服务私有实现；创建、RID 包装、延迟销毁和内容采用均属于服务。
网格、纹理从统一 CPU 请求领取共享引用后直接传给对应采用函数，不再重新组装 decoded_asset。
来源身份检查仍在采用前执行；材质在依赖完成后直接传递已经持有的纹理强引用。
资产记录以 `weak_ptr<const asset>` 直接弱持有最终对象，并保存统计、同步创建递归保护和当前在途操作。
缓存写入验证实际类型与键一致；查询与内部解码通过类型化指针转换共享控制块，支持基类地址偏移。
`cached_asset` 只是查询时取得的强引用与统计副本，不建立控制块或永久缓存身份。
渲染资产内部的 CPU 解码不登记资产索引或临时 payload 键，只由父加载操作持有请求；
同资产请求在最终资产记录上合并，中间解码复用执行队列与取消逻辑。
`load_operation` 统一保存记录弱关联并检查观察者；清理仅解除指向自身的当前操作。
渲染装配在采用前检查当前操作身份，旧操作停止不会清除新操作。具体 I/O、CPU 与依赖取消仍由对应阶段执行。
ActionMap 使用 `load<action_map>` 返回共享资产；`snapshot()` 取得不可变
`action_map_description`。重载保持资产身份和旧快照，InputService 显式复制并校验描述，
不自动跟随重载。描述使用原有 input-map 文件格式和 GUID；描述的 load/store/find_loaded
返回 unsupported，显式 Decoder 注册到 action_map_description 供 Loader 使用。
没有独立批次加载器、资源工厂或 prepare/apply 更新记录，也不在资源对象上保存更新序号。
Runtime 的 `resource_load_group` 仅为场景与编辑器串行组合单资源请求、汇总显式请求结果；
它不读文件、不解析依赖、不创建后端资源，且不占用资源服务的全局加载槽位。


asset_manager 的 load 请求通过 files::service 读取有界字节；每阶段文件完成回调交付字节后提交一次
asset-decode 任务。等待文件期间不轮询 CPU 任务，不使用同步文件适配。
普通类型解码器只消费字节。宿主先分发文件完成，再由 advance 推进解码结果登记。

asset_manager 仅接受 shared_ptr<files::service>。`find_loaded<T>` 只查询已完成缓存，
未命中返回 not_found；`load<T>` 返回请求，不阻塞等待文件或解码。
`register_decoder<T>(function)` 可直接登记
`result<T, serialization::diagnostic>(span<const byte>)` 函数，无需类型继承或反射身份。
仍可继承 asset_decoder 提供有状态解码器；回调必须支持不同输入的并发调用。

自定义资产加载优先使用显式 decoder；未注册时，按 type_id 查找冻结 Registry 中的 decode。
没有解码能力时在读取前返回 unsupported；显式解码失败不再回退。适配器在临时对象上解码，
成功才进入缓存，失败保留既有缓存资源。`asset_request::take()` 返回的失败结果携带 codec 的路径、消息和字节位置。

首次有效加载进入解码器选择后冻结显式注册，后续反射适配不改变 codec 选择。
管理器使用进程共享的 Meta Registry，宿主须先完成
[默认注册表初始化](reflection.md#默认注册表与启动)。内置类型包含 World 属性以及材质、Scene、Prefab codec 和 ActionMap codec。
构造时要求默认 Registry 已冻结，未冻结时抛出 invalid_argument。
普通加载不传递字节额度，按注入文件服务的 max_file_bytes 读取。
Scene / Prefab、ActionMap 等格式自己的长度校验仍在 codec 中执行，不再通过类型 traits 限制读取。

功能层 `asset_manager` 统一负责资产加载编排。材质通过 `load<material>` 加载，其描述由内部解码请求取得。
资源服务在所属线程领取描述并请求纹理依赖，工作线程只解码 CPU 数据。
网格和纹理由 asset_manager 的类型化入口加载，与描述请求共用文件读取、任务调度和关闭流程。
网格读取后解码一次；纹理先读取并解析描述，再读取负载，按请求携带的设备能力解码。
描述与负载分别按文件服务上限读取；相同身份的 CPU 请求共享在途工作与只读结果。
资源服务领取 CPU 数据、校验来源并装配依赖，不直接访问文件服务或解码执行器。
材质领取缓存描述并装配纹理强引用，字节统计保留溢出检查。

texture_description 表示现有纹理 JSON 的运行时视图；尺寸、变体和 mip 布局仍来自容器 Manifest。
网格复用 GNMS 文件头，不增加重复描述文件。当前不是 mip/LOD 流送。
asset_manager 不设统一的单资源输出额度，也不累计描述与负载的读取额度。
文件服务集中配置单文件大小及在途缓冲预算；Application 当前配置单文件 64 MiB，可由宿主调整。
格式解码器保留长度、数量、尺寸与整数溢出检查；PNG 解码保留默认 256 MiB 像素输出保护，
这是压缩展开保护，不是通用加载额度。KTX2 当前只接受无超级压缩的 RGBA8 数据。
总驻留预算、流送与淘汰尚未实现。
多个纹理变体可能全部被读取，但有设备 profile 时只保留选中变体的 CPU 负载和 Manifest。
运行异步路径释放原文件来源，GPU 恢复直接复用 CPU 数据；代价是该数据随资源引用常驻。
旧 prepare_assets/prepare_texture 和分步 asset_preparation 接口已删除。

加载回执的 applied 表示 CPU 内容已被采用，不表示 GPU 已可用；上传与 GPU 生命周期由 Render 独立管理。
读取失败不上传半成品，更新失败保留旧内容；取消请求放弃观察，迟到文件回调只持有弱引用。
完整加载链路的实施边界见 [PLAN-052](../plans/PLAN-052-async-render-assets.md)。
反射 fallback 调用登记的函数，不代表自动遍历属性反序列化。

### Scene 与 Prefab 描述

`scene` 和 `prefab` 通过同一 manager 异步加载，专用 Loader 解码对应描述格式；
保留层级及 Prefab 单根校验，未知扩展字段不保留。每文件默认限制为 16 MiB。
直接 parse/serialize 函数仍用于编辑器的内存文本操作。
Prefab 文件使用 `load<prefab>()`，需要描述时调用资产的 `snapshot()`；
显式重载使用 `reload=true`。

场景所属线程用 `scene_description_request` 取得场景及去重后的 Prefab 引用；工作线程只做
层级排序、依赖收集与准备预算检查。`scene_prepare_limits::source_bytes` 统计累计源文件
输入字节，不代表解析对象的内存。请求成功 take 后可查询 source_bytes()，缓存命中保留该统计；
场景加载输入传递累计值，纯内存构造的输入为零。描述不保留源文本，单文件输入限制仍由
manager 的有界读取保证。保存行为见[场景格式](scene-format.md)。

普通同步加载复用描述缓存；显式重载与后台打开场景/准备 Prefab 会重新加载。
准备结果只持有取得的 Prefab 引用、父节点优先索引和最大深度，不复制完整描述。
创建实例时直接采用该引用；缺失引用返回 invalid_argument，不回退到缓存登记。
场景实例应用覆写所需的可变数据仍由实例单独持有，不修改共享来源。
`store<T>` 用于显式采用已构造的数据，成功后较早请求只能返回各自结果，不能覆盖当前缓存；
失败保留原输出。`store<T>` 不执行 codec 校验，调用者负责提供已验证的内存对象。
World、场景实例及编辑会话仍由场景服务管理，不进入资源缓存。

### 描述与实例的持有关系

| 对象 | 谁持有 | 释放与更新 |
| --- | --- | --- |
| Scene 描述快照 | scene 资产、准备输入及实例 | 资产内容重载不改变旧快照，编辑副本独立持有 |
| Prefab 描述快照 | prefab 资产、准备结果、实例及编辑器历史 | 实例和历史可保留旧快照；覆写副本与共享来源分开 |
| Material CPU 描述 | 内部加载操作 | 采用后可释放；最终材质持有参数及纹理引用，失败保留旧运行期数据 |
| ActionMap | manager 缓存、请求；InputService 持有采用后的独立值 | 采用时复制并校验，失败保留原映射；成功后无需长期持有资产引用 |

`stop()` 排空 manager 任务并结束在途请求，不等于立即清空成功缓存；缓存随管理器销毁，
或通过 `collect_unused()` 回收。已持有的 CPU 引用可继续存活。Application 关闭时先停止资源任务，
再关闭场景服务并释放场景/渲染加载器及 manager，最后关闭后端与 I/O 执行器。
外部渲染引用仍必须遵守渲染服务生命周期，不能照搬 CPU 引用跨管理器存活的规则。

## 纹理二进制容器结果

`encode_texture_binary` 返回 `result<std::vector<std::byte>, texture_binary_diagnostic>`，
`decode_texture_binary_header` 返回布局结果，`decode_texture_binary` 返回视图结果。
三个接口均无数据或诊断输出参数。`texture_binary_diagnostic` 包含 `texture_binary_error error`
及自有 `std::string message`；成功由 result 的值分支表示，错误枚举没有 success 成员。
诊断文本分配失败返回 out_of_memory 和空消息；编码失败不返回部分字节。
解码视图仍借用输入字节，不能超过输入存储的生命周期。64 字节 Header 与封装布局不变。


## 描述编解码入口

材质、Action Map、Scene 与 Prefab 使用 `serialization::json::serialize_bytes(value)` 与 `serialization::json::deserialize<T>(bytes)`，
不再提供按类型命名的转发 codec。调用前仍须初始化 Meta 注册表。
资源层内部 `register_reflected_asset` 复用已配置的类型 builder，追加统一格式包装并提交；
格式名、当前版本和字节上限由各资源的格式配置决定，版本错误码使用统一规则。
材质保持 gneiss.material v6，输入映射保持 gneiss.input-map v1 及 1 MiB 上限。
该辅助不替代字段及业务校验，也不改变二进制资源的专用协议。

Scene v5 与 Prefab v1 同样使用通用格式注册，保持各自 16 MiB 限制。
格式版本统一要求正整数：非当前正整数版本返回 unsupported，其余非法版本返回
invalid_argument；不再按资源类型或版本新旧采用不同错误码。重复 format/version 拒绝。

## Scene/Prefab 资产与快照

`resource/scene.hpp` 和 `resource/prefab.hpp` 定义共享资产对象。通过
`asset_manager::load<scene/prefab>()` 异步加载，返回 `ref<const scene/prefab>`；专用
`scene_loader` 复用描述的文件格式与反射解码，不创建 World 实例、不等待视觉资源。
`source()` 保存来源域和 URI，`type()` 标识具体资产类型。

所属线程调用 `snapshot()` 取得只读描述引用，可交给工作线程。成功重载保持资产对象身份，
替换当前快照；已取出的快照继续保留旧内容，失败不更新内容。快照本身不保活资产身份，
需要后续共享加载身份的调用方须持有资产引用。World 不会因资产重载自动改变。

场景准备、运行实例、捕获结果与编辑历史同时保留来源资产强引用和当次描述快照。
身份引用保证同 URI 继续共享资产；描述快照固定实例采用的内容，编辑不会写回共享资产。
纯内存创建的编辑对象允许没有来源资产。加载生命周期统一仍处于迁移期，
最终缓存与引用入口的收敛见 [PLAN-102](../plans/PLAN-102-resource-object-loading.md)。

Scene/Prefab 描述的 `load/store/find_loaded` 和对应 Decoder 注册返回 `unsupported`。
描述不能单独登记 URI 缓存；文件加载统一返回资产对象。反射编解码与不登记缓存的
`create(description)` 仍可用；编辑快照由调用方持有，不覆盖文件资产内容。
