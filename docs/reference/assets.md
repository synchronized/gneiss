<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 资产 URI、目录挂载与缓存

普通资源、网格和纹理统一通过[文件服务](file-service.md)异步读取；旧 VFS 和同步加载入口已删除。

## URI 规则

现有资产加载入口当前只支持规范形式的 `asset://` URI，例如：

```text
asset://models/triangle.mesh
asset://纹理/石头.png
```

URI 使用 UTF-8 和正斜杠，scheme 区分大小写。空路径、空路径段、`.`、`..`、控制字符、反斜杠、
冒号、查询、片段和百分号编码均返回 `engine::result::invalid_argument`。原生校验函数不访问文件系统：

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
| `asset_manager::find_loaded<T>`、`asset_request<T>::take` | `asset_ref<const T>`，直接访问 CPU 描述 | 成功重载替换当前缓存，旧引用继续保活旧数据 |
| Prefab 描述 | `prefab_asset_ref`，即 `asset_ref<const prefab_description>` | 别名引用保活缓存条目；重载不会修改旧描述 |
| 资产管理器、场景绑定、编辑器历史 | `mesh`、`material`、`texture` 强引用；混合批次另用缓存条目 | CPU 内容采用即完成，资源身份保持稳定，GPU 更新由 Render 独立处理 |

渲染引用通过 `asset_rid`、`asset_kind`、`asset_uri` 访问 RID、类型和 URI；空引用分别返回
零 RID、invalid 类型和空 URI。场景绑定校验所属服务及来源域，ECS 保存类型化资源强引用。
上传任务和在途渲染帧对具体数据的共享所有权不等同于资产身份引用。

类型化运行对象分别定义在 `resource/mesh/mesh_resource.hpp`、
`resource/texture/texture_resource.hpp` 和 `resource/material/material_resource.hpp`。
上层包含所需资源头，保存 `mesh`、`texture`、`material` 强引用；材质持有纹理强引用。
资源对象析构只处理 CPU 状态与依赖，不调用 Render。管理器的延迟销毁队列先在所属线程
释放 RID，再析构资源对象；服务关闭后的晚释放只析构 CPU 对象。
CPU 数据表位于 `render_service` 私有实现，Loader 与宿主通过其 RID 接口操作。
功能层资产管理器负责强引用创建、元信息及材质依赖，详见 [Render 资源与服务边界](render.md)。


`resource_cache` 位于 `engine/function/resource/resource_cache.hpp`，与资产管理器同属功能层。
`asset_manager` 唯一持有 `resource_cache::entries_`，统一按「来源域 + URI + C++ 类型」索引。
通用 CPU 资产、Prefab 和渲染资产共用此表；同 URI 的不同类型或来源各自独立。
类型键使用 `engine::type_id`，由同一 Engine 按规范 C++ 类型名集中分配，
只在当前运行期有效，不写入资产文件或公共 ABI；约束见 [Core 身份类型](core.md#内部基础身份类型)。
通用请求表只保留请求顺序和在途观察状态，不保存另一份当前资源。
通用异步加载默认复用成功缓存；显式 `reload=true` 才重新读取。
资源表统一通过 `resource_cache::load` 查询、按需执行回调并登记成功结果；显式重载失败
（包括回调抛异常或返回空资源）保留旧条目，首次失败不残留占位。相同键递归加载返回
`invalid_state`，不同键的依赖仍可递归加载。Prefab 普通加载和重载共用解析流程；
渲染预留在缓存未命中时才创建 RID，服务归属和 GPU 就绪仍由 Render Loader 检查。
CPU 解码由注入的执行器运行，完成结果在所属线程通过同一入口登记；协作模式在所属线程执行解码。

请求取消只放弃当前观察者，不使其他引用失效。

缓存持有强引用，在所属线程调用 `collect_unused()` / `release_unused()` 回收无使用者的条目。
Application 和场景统一调用管理器回收；Render Loader 的回收入口也只转发到这张共享表。
回收同时覆盖 CPU、Prefab 和渲染条目，保留在途请求索引；CPU 重载任务暂时持有旧条目，
避免回收与失败重载叠加导致旧内容提前消失。
已完成但尚未领取的请求也保活结果。关闭通用管理器不使已领取的 CPU 描述引用失效。
失败不永久缓存，重载失败保留当前成功数据；输出参数的失败行为以具体接口契约为准。

引用计数并不提供资源数据的线程安全。CPU 数据可按只读契约共享。运行渲染资源的最后释放可在
任意线程发生，删除器仅入队，由所属线程析构；服务关闭后走 detached 路径。
GPU 在途使用的安全回收由 Render 及后端负责，详见[运行资源生命周期](render.md#运行资源身份与销毁)。
缓存查询与资源采用限所属线程；异步文件服务与 CPU 解码执行器分别负责 I/O 和解码。

引用规则见 [ADR-077](../decisions/ADR-077-asset-references.md)，统一索引见
[ADR-078](../decisions/ADR-078-unified-asset-index.md)。

## 内部异步准备

网格、纹理与材质共用纯内存解码入口 `decode_loaded_asset`，该入口不读取文件、不递归加载依赖。
文件服务返回字节后，加载编排层提交 CPU 任务；thread 与 cooperative 使用同一解码实现。
协作模式的单次 JSON/PNG 解码仍不可抢占，不承诺单帧时间上界。

准备结果拥有网格数组或纹理 CPU 内容，材质依赖以 URI 返回，由加载编排层继续请求。
纹理 Manifest 和所选负载摘要仍校验；CPU 负载常驻供 Render 重建，不保留可重新打开的文件来源。
文件更新由后续加载处理，不保证多个文件属于同一时刻。文件读取失败、取消和格式错误分别通过结果码报告。

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
材质仅读写 v5，字段及默认值见 [Render 资产格式](render-asset-formats.md)。
`material_resource` 是稳定运行对象，保存 `material_parameters` 和五槽 `texture` 强引用，
通过只读 `parameters()`、`textures()` 查询；它不再持有描述对象或纹理缓存条目。
后端 CPU 快照 `material_data` 保存同结构参数及纹理 RID。参数默认白色、不透明、
metallic=0、roughness=1；这不免除材质文件必填字段的校验。
运行期数据由渲染服务管理；后端 `material_mirror` 持有 Granit 对象。
描述不包含 RID、GPU 对象或编辑会话状态。可通过 register_material_reflection 显式登记编解码能力，
接口见[反射参考](reflection.md#类型编解码)。

资源领域命名按职责区分：`*_description` 表示可持久化的描述，`*_parameters`/`*_settings`
表示参数，`*_data` 表示实际数据，`*_resource` 表示服务使用的运行期资源；
`*_document` 留给编辑器文档，`prepared_*` 与 `*_mirror` 表示准备数据及后端镜像。
此规则不要求数学值、网格数组等所有可序列化类型都使用 description 后缀，
也不将任务调度等其他领域的 description 解释为可持久化资源。

## 普通资源读取与解码

管理器位于 `engine/function/resource`。宿主构造管理器后用 `attach_render(render_service&)` 绑定
渲染服务，再用 `configure_loading(executor, profile, notification)` 装配异步加载。
纯 CPU 使用者只需 `attach_task_executor`，不要求 Render。
Scene/Editor 借用管理器，不再独立创建 Loader、Factory 或 TextureLoadService。
`load<T>` 保留 CPU 描述解码契约；带纹理依赖的网格、材质和纹理使用 `submit_assets` 批次接口，
通过 `take(texture_load_completion&)` 领取结果。两种请求共用管理器、文件服务和资源索引，
本次没有将批次结果另包成一套模板请求队列。
宿主统一调用 `advance_resources`（`advance` 为同义入口）；
关闭调用 `request_stop_resources` 并推进至 `resources_stopped`，最后销毁管理器，再关闭 Render。
内部加载文件位于 `detail/`，不向宿主开放。资源数据及格式编解码仍位于 `engine/resource`。

asset_manager 的 load 请求通过 files::service 读取有界字节；完成回调交付字节后只提交一次
asset-decode 任务。等待文件期间不轮询 CPU 任务，不再使用 asset_source。
普通类型解码器只消费字节。宿主先分发文件完成，再由 advance_resources 推进解码结果登记。

asset_manager 仅接受 shared_ptr<files::service>。`find_loaded<T>` 只查询已完成缓存，
未命中返回 not_found；`load<T>` 返回请求，不阻塞等待文件或解码。
`register_decoder<T>(function, maximum_bytes)` 可直接登记
`result(span<const byte>, T&, serialization_diagnostic&)` 函数，无需类型继承或反射身份。
仍可继承 asset_decoder 提供有状态解码器；回调必须支持不同输入的并发调用。

加载优先使用显式 decoder；未注册时，按 type_id 查找冻结 Registry 中的 decode。
没有解码能力时在读取前返回 unsupported；显式解码失败不再回退。适配器在临时对象上解码，
成功才进入缓存，失败保留既有资源和调用者输出。`asset_request::take` 的可选 diagnostic 参数返回 codec 的路径、消息和字节位置。

首次有效加载进入解码器选择后冻结显式注册，后续反射适配不改变 codec 选择。
管理器持有 Registry；默认使用共享的内置资源 Registry，目前包含材质、Scene、Prefab codec 和 ActionMap 解码器。
构造时也可传入已冻结的自定义 Registry，未冻结或零输入上限抛出 invalid_argument。
反射读取默认上限为 64 MiB，可通过构造参数指定；类型声明 `maximum_source_bytes` 时取两者较小值。
ActionMap 限制为 1 MiB，解析器也检查此上限；显式 decoder 使用自己的上限。

功能层 `asset_manager` 同时拥有 CPU 描述加载与运行资源加载编排。材质描述通过 `load<material_description>` 加载。
加载编排层在所属线程领取描述，工作线程只处理只读数据和纹理依赖，不访问管理器。
管理器内部的网格和纹理加载使用 async_files() 返回的同一文件服务，
通过 manager.identify() 解析来源域。CPU 描述与渲染资源以不同类型键保存在同一索引中。
网格完成有界整读后提交一次纯内存解码；纹理先读取描述并解析依赖 URI，再读取完整负载，
随后解码。CPU 任务不推进文件请求，也不采用同步文件回退。材质领取描述后也使用纯内存准备入口。

texture_description 表示现有纹理 JSON 的运行时视图；尺寸、变体和 mip 布局仍来自容器 Manifest。
网格复用 GNMS 文件头，不增加重复描述文件。当前不是 mip/LOD 流送：每项输入与解码输出各有
64 MiB 上限，纹理描述和负载输入合计也受该上限限制，批次准备总量上限仍为 256 MiB；
注入文件服务须配置可接受相应 max_bytes 的上限，否则提交返回 invalid_argument。
多个纹理变体可能全部被读取，但有设备 profile 时只保留选中变体的 CPU 负载和 Manifest。
运行异步路径释放原文件来源，GPU 恢复直接复用 CPU 数据；代价是该数据随资源引用常驻。
旧 prepare_assets/prepare_texture 和分步 asset_preparation 接口已删除。

加载回执的 applied 表示 CPU 内容已被采用，不表示 GPU 已可用；上传与 GPU 生命周期由 Render 独立管理。
读取失败不上传半成品，更新失败保留旧内容；取消请求放弃观察，迟到文件回调只持有弱引用。
完整加载链路的实施边界见 [PLAN-052](../plans/PLAN-052-async-render-assets.md)。
反射 fallback 调用登记的函数，不代表自动遍历属性反序列化。

### Scene 与 Prefab 描述

`scene_description` 和 `prefab_description` 通过同一 manager 同步或后台加载，默认 Registry
注册现有格式的双向 codec；保留未知字段、层级及 Prefab 单根校验。每文件默认限制为 16 MiB，
显式 decoder 可以指定自己的上限。直接 parse/serialize 函数仍用于编辑器的内存文本操作。
Prefab 直接使用 `load_sync<prefab_description>` / `load<prefab_description>`；
显式重载使用 `reload=true`，不再保留专用 Prefab Loader 或转发接口。

场景所属线程用 `scene_description_request` 取得场景及去重后的 Prefab 引用；工作线程只做
层级排序、依赖收集与准备预算检查。`scene_prepare_limits::source_bytes` 统计累计保留的
`author_json` 字节，不是所有解析对象的内存，也不是自定义 decoder 的原始输入字节总量。
默认 codec 保留原文；单文件输入限制仍由 manager 的有界读取保证。

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
| Scene CPU 描述 | manager 缓存、加载请求及准备输入 | 场景实例持有自己的可变描述；准备输入释放后，缓存描述可在无外部引用时回收 |
| Prefab CPU 描述 | manager 缓存、准备结果、Prefab 实例及编辑器历史 | 实例与历史持有旧来源时不会被强制回收；覆写副本与共享来源分开 |
| Material CPU 描述 | manager 缓存、准备/上传任务、运行期材质 | 运行期更新成功才切换描述与纹理依赖；失败保留旧运行期数据 |
| ActionMap | manager 缓存、请求；InputService 持有采用后的独立值 | 采用时复制并校验，失败保留原映射；成功后无需长期持有资产引用 |

`stop_resources()` 排空 manager 任务并结束在途请求，不等于立即清空成功缓存；缓存随管理器销毁，
或通过 `collect_unused()` 回收。已持有的 CPU 引用可继续存活。Application 关闭时先停止资源任务，
再关闭场景服务并释放场景/渲染加载器及 manager，最后关闭后端与 I/O 执行器。
外部渲染引用仍必须遵守渲染服务生命周期，不能照搬 CPU 引用跨管理器存活的规则。
