<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 资产 URI、目录挂载与缓存

独立文件服务核心见[异步文件服务](file-service.md)；当前资产加载尚未迁移到该入口。

## URI 规则

现有资产加载入口当前只支持规范形式的 `asset://` URI，例如：

```text
asset://models/triangle.mesh
asset://纹理/石头.png
```

URI 使用 UTF-8 和正斜杠，scheme 区分大小写。空路径、空路径段、`.`、`..`、控制字符、反斜杠、
冒号、查询、片段和百分号编码均返回 `engine::result::invalid_argument`。原生校验函数不访问文件系统：

```cpp
#include "engine/resource/vfs/asset_uri.hpp"

auto result = gneiss::engine::validate_uri(uri);
```

## VFS 与资产根目录

`gneiss_application_desc.asset_root` 与 `asset_root_length` 可在创建 Application 时挂载一个 UTF-8
目录。两者必须同时为空或同时有效；目录不存在时创建返回 `GNEISS_ERROR_NOT_FOUND`。旧版结构大小
仍可创建不挂载资产根的 Application。

Application 内部将资产根作为 `asset://` 根挂载到虚拟文件系统（VFS）。VFS 按最长挂载点前缀
路由，文件系统后端只接收相对路径，因此未来可以把子目录挂载到归档或网络后端而不修改 Loader。

当前唯一实现是只读 `native_file_system`。它读取普通文件，并解析真实路径确认目标仍位于资产根内，
因此路径穿越和指向根外的符号链接不会被访问。VFS 后端接口目前保持内部；尚未实现目录枚举、写入、
归档或网络后端。Windows 原生范围来源另提供可轮询 I/O；类型 Loader 通过该 VFS 读取文件。

## 缓存与生命周期

内部资源强引用统一使用 `engine::asset_ref<T>`，底层共享标准智能指针控制块。
复制引用保活同一拥有者，移动转移引用，`reset()` 放弃当前引用；最后一个强引用释放时才析构
资源或执行原控制块删除器。引用不会自行加载资源，也不表示 GPU 内容已就绪。

| 入口 | 强引用与访问 | 更新规则 |
| --- | --- | --- |
| `asset_manager::load_sync<T>`、`asset_request<T>::take` | `asset_ref<const T>`，直接访问 CPU 描述 | 成功重载替换当前缓存，旧引用继续保活旧数据 |
| Prefab 描述 | `prefab_asset_ref`，即 `asset_ref<const prefab_description>` | 别名引用保活缓存条目；重载不会修改旧描述 |
| Render Loader、场景绑定、编辑器历史 | `render_asset_ref`，即缓存条目的 `asset_ref` | 普通重载保持 RID 稳定，上传完成后更新内容 |

渲染引用通过 `asset_rid`、`asset_kind`、`asset_uri` 访问 RID、类型和 URI；空引用分别返回
零 RID、invalid 类型和空 URI。场景绑定仍校验资源类型与所属服务，ECS 只保存 RID。
上传任务和在途渲染帧对具体数据的共享所有权不等同于资产身份引用。

`asset_manager` 唯一持有 `resource_cache::entries_`，统一按「来源域 + URI + C++ 类型」索引。
通用 CPU 资产、Prefab 和渲染资产共用此表；同 URI 的不同类型或来源各自独立。
类型键使用 `engine::type_id`，由同一 Engine 按规范 C++ 类型名集中分配，
只在当前运行期有效，不写入资产文件或公共 ABI；约束见 [Core 身份类型](core.md#内部基础身份类型)。
通用请求表只保留请求顺序和在途观察状态，不保存另一份当前资源。
通用同步和异步加载默认复用成功缓存；显式 `reload=true` 才重新读取。
资源表统一通过 `resource_cache::load` 查询、按需执行回调并登记成功结果；显式重载失败
（包括回调抛异常或返回空资源）保留旧条目，首次失败不残留占位。相同键递归加载返回
`invalid_state`，不同键的依赖仍可递归加载。Prefab 普通加载和重载共用解析流程；
渲染预留在缓存未命中时才创建 RID，服务归属和 GPU 就绪仍由 Render Loader 检查。
CPU 异步解码在工作线程执行，完成结果在所属线程通过同一入口登记。

请求取消只放弃当前观察者，不使其他引用失效。

缓存持有强引用，在所属线程调用 `collect_unused()` / `release_unused()` 回收无使用者的条目。
Application 和场景统一调用管理器回收；Render Loader 的回收入口也只转发到这张共享表。
回收同时覆盖 CPU、Prefab 和渲染条目，保留在途请求索引；CPU 重载任务暂时持有旧条目，
避免回收与失败重载叠加导致旧内容提前消失。
已完成但尚未领取的请求也保活结果。关闭通用管理器不使已领取的 CPU 描述引用失效。
失败不永久缓存，重载失败保留当前成功数据；输出参数的失败行为以具体接口契约为准。

引用计数并不提供资源数据的线程安全。CPU 数据可按只读契约共享；渲染资产的最后释放仍限
所属线程，渲染服务必须晚于资产引用销毁。GPU 在途使用的安全回收由渲染服务及后端负责，
`asset_ref` 不直接调用 GPU API，也不隐式切换销毁线程。
公共同步 API 仍限 Application 创建线程；异步服务使用注入的任务执行器。

引用规则见 [ADR-077](../decisions/ADR-077-asset-references.md)，统一索引见
[ADR-078](../decisions/ADR-078-unified-asset-index.md)。

## 内部异步准备

Asset 层拥有准备状态、输入缓冲、候选和来源身份。每个执行步骤共用读取字节预算，
并限制状态转换次数；短操作可在同一步连续完成，避免每个转换都等待下一宿主帧。
时间边界在操作之间检查，不能抢占单次 JSON/PNG 解析或同步文件系统调用。
thread 与 cooperative 使用同一实现；cooperative 在步骤之间返回宿主，没有隐式后台线程。

VFS 使用 `open_read` / `begin_open_read`，只负责打开和读取，没有验证回执。
准备流程保留本次实际读取内容的来源摘要；完成后不重新打开来源或做整批复验。
材质参数和网格数据由加载结果直接持有，不再通过独立 CPU 文档缓存按内容去重。
网格上传准备使用移动构造建立共享所有权，渲染资源及在途帧释放最后一个引用后销毁数据。
文件更新由后续加载或重载处理，不保证多个文件属于同一时刻。短读、取消和格式错误仍返回失败。
纹理容器的 Manifest/负载摘要校验与旧 GPU 数据恢复的摘要匹配仍保留。
只提供整文件读取的内部自定义后端保留同步回退，不承诺同样的单步时间上界。
Windows 原生后端提供 OVERLAPPED 范围请求，跨步骤保留自有缓冲；NOT_READY 时返回宿主，
请求销毁前完成取消回收。同步 API 不采用轮询忙等。Linux 原生范围读取及未实现这一可选契约的
后端仍同步执行，不保证文件系统等待上界；不保证外部文件内容不可变。

## 场景请求的清理快照

场景候选尚未发布时，取消会停止后续上传批次；已经提交的上传仍等待回执，
再回滚未发布的 GPU 投影。回滚结束前不销毁上传租约，也不报告清理完成。
上传中的场景取消可以先返回唯一取消终态，同时以 `cleanup_pending=true` 明确资源仍在清理。
清理期间不能激活候选或接受重试；完成后通过进度查询获得账本补报，不产生第二次终态。
普通资产重载获得上传许可后仍完成或回滚原事务，不改变其迟到取消语义。

Runtime 的 `scene` 域消息可携带内部 `budget` 对象。容量字段为无符号 64 位字节数：
`candidate_logical_bytes`、`candidate_cpu_data_bytes`、`application_logical_bytes`、
`application_cpu_data_bytes`、`available_bytes`、`upload_reserved_bytes`、`peak_upload_bytes`。
候选为已完成批次统计，Application 为资源账本，上传预留为当前子批次估算；均不是进程 RSS 或实际显存。

可选布尔字段 `cleanup_complete` 只有在失败/取消请求的内部状态和上传租约销毁后才为 true。
此时候选分项保留回收前的诊断值，Application 和上传预留重新采样回收后的账本。
活动场景、共享缓存和旧渲染帧仍可合法持有资源；该字段不表示整个 Application 已清空。
成功激活将候选转移给活动场景，不使用这个标记表示释放。
旧消息省略该字段按 false（未确认）处理；错误类型被拒绝，既有 IPC 域版本不变。

`cleanup_pending` 同样是可选布尔字段，缺省 false，不能与 `cleanup_complete` 同时为 true。
Runtime 的清理补报为事件，不重复发送请求响应；Editor 仅更新同一取消请求的预算。
生命周期依据见 [ADR-058](../decisions/ADR-058-scene-cancellation-cleanup.md)。

## 资源描述与运行期材质

`material_description` 是唯一的 CPU 材质描述，包含 `material_parameters parameters` 和
五槽纹理 URI；解析结果直接使用此类型，不再增加单字段文档包装。
材质仅读写 v5，字段及默认值见 [Render 资产格式](render-asset-formats.md)。
`material_resource` 与描述共用 `material_parameters parameters`，纹理依赖改用 RID；
加载时整体复制参数，渲染服务与后端直接读取同一参数结构。参数默认白色、不透明、
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

asset_manager 通过 VFS 读取有界字节，再调用 asset_decoder；解码器不接收 URI 或 VFS。
`register_decoder<T>(function, maximum_bytes)` 可直接登记
`result(span<const byte>, T&, serialization_diagnostic&)` 函数，无需类型继承或反射身份。
仍可继承 asset_decoder 提供有状态解码器；回调必须支持不同输入的并发调用。

同步和异步加载都优先使用显式 decoder；未注册时，按 type_id 查找冻结 Registry 中的 decode。
没有解码能力时在读取前返回 unsupported；显式解码失败不再回退。适配器在临时对象上解码，
成功才进入缓存，失败保留既有资源和调用者输出。`load_sync` 和 `asset_request::take` 的可选 diagnostic 参数返回 codec 的路径、消息和字节位置。

首次有效加载进入解码器选择后冻结显式注册，后续反射适配不改变 codec 选择。
管理器持有 Registry；默认使用共享的内置资源 Registry，目前包含材质、Scene、Prefab codec 和 ActionMap 解码器。
构造时也可传入已冻结的自定义 Registry，未冻结或零输入上限抛出 invalid_argument。
反射读取默认上限为 64 MiB，可通过构造参数指定；类型声明 `maximum_source_bytes` 时取两者较小值。
ActionMap 限制为 1 MiB，解析器也检查此上限；显式 decoder 使用自己的上限。

材质描述可直接 `load<material_description>` / `load_sync<material_description>`。
渲染 Loader 借用同一个 asset_manager；同步加载、同步重载和后台材质请求均采用该管理器的
显式 decoder/反射选择规则。普通同步加载复用 CPU 描述缓存，reload 才重新读取；Input 的
显式重新加载入口仍强制读取。缓存命中不使较早异步请求失去写入权，成功同步读取才覆盖它。
后台协调者在所属线程发起请求并领取描述，工作线程只处理只读描述和纹理依赖，不访问 manager。
asset_preparation 不读取或解码材质文件，材质输入缺失返回 unsupported。运行期材质持有对应描述引用，
更新成功后切换引用；失败保留旧 RID 内容与描述。CPU 描述和渲染资源以不同类型键保存在同一索引中。
纹理和网格仍使用专用范围读取及准备流程。
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
