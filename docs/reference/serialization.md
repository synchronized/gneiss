<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 序列化接口

契约位于 `engine/include/gneiss/engine/core/serialization`，实现位于 `engine/src/core/serialization`，使用 Meta 的类型和属性访问，不维护第二套注册表。

## 内置注册

`builtin_registration.hpp` 的 `serialization::register_builtin_types()` 注册基础值、数学类型
及其持久化策略；显式注册表重载也可用。入口不冻结表，重复调用按普通重复注册报错。
应用的 initialize_meta 统一调用它，再注册资源及用户类型，最后冻结。
Meta 只提供通用注册器与扩展存储，不依赖这些持久化策略。

## 调用入口

普通 JSON 使用 `json.hpp`：

```cpp
namespace ser = gneiss::serialization;
auto text = ser::json::encode(description);
auto bytes = ser::json::encode_bytes(description);
auto restored = ser::json::decode<scene_description>(json_text);
```

使用其他后端时包含 `serialization.hpp`，传入实现了 reader/writer 的对象：

```cpp
auto status = ser::write(description, output_writer);
auto restored = ser::decode<scene_description>(input_reader);
```

写入以及 read_into 返回 `result<void, serialization::diagnostic>`，布尔为真表示成功；
失败时通过 error() 获取诊断，不能在成功时读取 error()。
构造对象的读取返回 `result<T, serialization::diagnostic>`。
所有显式上下文重载采用 context-first：

```cpp
ser::write_context write;                // 使用默认注册表
ser::read_context read{registry, 64};    // 指定注册表与最大嵌套深度
auto text = ser::json::encode(write, description);
auto restored = ser::json::decode<scene_description>(read, json_text);
auto status = ser::write(write, description, output_writer);
```

编码和 capture_value 只接受 write_context；解码、apply_value 和 restore_value 只接受 read_context。
两个方向不能互换；只有 read_context 可借用 reference_resolver，编码不持有解析器。
无 context 的类型模板使用默认注册表；视图重载使用视图绑定的注册表。
显式 context 与视图必须绑定同一注册表，否则返回 invalid_argument，不修改读取目标。
显式空注册表返回 invalid_handle，不回退默认表；context 不自动注册或冻结类型。
无效输入、未注册类型及后端错误不返回半成品对象；写入已有 instance 的重载不承诺回滚。

## 共享遍历

`traversal.cpp` 负责字段遍历、枚举名称、sequence、associative、optional、variant、
内嵌引用与多态类型信封、引用环/深度检查、构造与校验、字段错误路径。
它不包含 yyjson，也不选择 JSON codec。

类型注册字段后默认支持共享遍历；未知字段处理通过 object_policy 扩展声明。
特殊类型通过 `.extend(serialization::codec<Encode, Decode>)` 注册 content_codec，按方向优先于字段遍历。
编码签名为 `(const write_context&, writer&, const T&) -> result<writer::node, diagnostic>`；
解码签名为 `(const read_context&, reader&, reader::node) -> result<T, diagnostic>`。
允许 nullptr 表示缺少某方向，该方向回退到字段遍历；解码仍需要类型构造能力。
自定义编码返回对象节点，解码返回独立对象；异常由序列化边界转换为错误。
Meta 不包含 JSON 类型或格式专用回调，reader/writer 不承担类型 codec 注册。
value 通过 variant_traits 接入通用 variant 遍历，对象分支复用通用多态引用，无专用 codec。

## 字段策略与引用优先级

通过 property(...)(serialization::field_policy{...}) 设置当前字段的持久化行为：

- save 默认为 true；false 时文件与快照均不保存该字段，解码忽略输入中的该字段，恢复保留默认值。
- required 默认为 false；true 时文件缺少字段报错，不能与 save=false 或内存只读组合。
- enum_name 仅用于直接枚举字段，按注册枚举名称读写。
- reference 是可选的引用策略。未设置时继承被引用类型策略；设置空 reference_policy 则显式恢复普通内嵌。

引用选择统一遵循字段覆盖、被引用类型默认、框架内嵌默认的优先级。
字段覆盖仅支持直接引用字段；容器字段配置 reference 返回 invalid_argument，容器元素仍遵循元素类型默认策略。
类型内容 codec 不受引用位置策略覆盖。内嵌多态仍使用 $type_guid/$content，外部引用按策略读写身份。

capture_value/apply_value 使用同一策略选择；外部引用通过 capture 回调保存资产身份并由 resolver 恢复，
缺少 resolver 明确失败。内嵌引用捕获独立内容，恢复不保留原共享别名。
自定义外部引用没有 capture 适配时不能静默作为内嵌对象捕获。

## 后端契约

reader/writer 提供对象、数组、空值、字符串及基础值操作。
节点句柄是不透明借用值，只能交回所属后端；null 句柄表示不存在或创建失败，
文件中的空值仍由有效节点表示。writer 创建的节点由 writer 后端管理。
reader 的键名和游标借用输入，不能超过输入及 reader 的生命周期。
context 的配置只读，递归状态私有；即使参数是 const 引用，也不能并发共用同一 context。
context 不可复制，可在操作完成或失败后顺序复用。路径、深度、循环引用及 reader
由最外层入口的栈上 operation_state 持有，context 仅在操作期间借用该状态。
嵌套入口复用状态并在返回时恢复深度和 reader；外层退出销毁状态，property_path() 返回空。
嵌套 codec 必须传递已有 context；新建 context 或使用无 context 入口会开始独立操作。
共享可变后端实例的并发访问也由调用方负责同步。

默认最大嵌套深度为 64，根为零，字段、容器和引用的结构层级递增；叶子标量不另加限制。
自定义 codec 重入完整公共入口会增加深度；字段委托保持当前深度。
JSON 的内部节点适配用于 IPC 与资源文件边界，不再作为类型注册回调。
回调只借用 context，不得在返回后保存其引用。

JSON 后端直接借用 yyjson 节点，不构建中间 DOM，不为共享遍历重新解析文本。
reader 的容器游标逐项前进，不额外分配整个节点列表。
writer 的结构操作返回 false 表示创建/挂接失败，统一转换为 out_of_memory；
scalar 和通用类型 codec 可返回带诊断的错误。
失败后的 writer 可能包含尚未设置为根的临时节点，其回收由后端负责。

当前接口面向内存中的结构编码，并非前向流式 writer。
没有通用二进制后端或 MessagePack 实现；Mesh/Texture 的专用二进制格式继续使用独立 codec。
设计取舍见 [ADR-105](../decisions/ADR-105-serialization-format-boundary.md)。

## 资源文件边界

普通 `serialization::json::encode(description)` 只保存描述内容，不添加文件头。
完整资源文件包含 `engine/resource/asset_file.hpp`：

```cpp
auto bytes = asset_file::serialize_bytes(scene);
auto loaded = asset_file::deserialize<scene_description>(file_bytes);
```

资源入口提供 context-first 及默认上下文版本；`format_traits<T>` 选择文件格式。
Material、Action Map、Scene、Prefab、Texture 的格式名、版本、大小限制集中在 asset_file.cpp。
读写复用反射字段，文件头只作用于最外层，不传播到嵌套描述对象。
format 的可选 validate 回调在写入前、读取独立对象后检查领域约束；Scene/Prefab 的整体身份、
层级和主相机检查由此入口执行，并递归校验对象 UUID、Prefab 引用与覆盖键，
这些规则不再注册到通用序列化。文档恢复继续显式校验局部描述和整体结构。
Action Map 的动作名称、绑定和按键/scale
约束也由文件边界执行；输入服务采用与自定义解码器加载后显式复用同一领域校验。
Mesh 描述的拓扑、数组对应关系和法线约束，以及 Texture 的来源、用途与颜色空间约束，
由所属资源模块校验并接入文件边界；直接解码纹理负载也先校验描述。
材质参数与采样约束由 validate_material_parameters 统一检查；描述文件边界补充 parameters
路径，资产采用和原生渲染创建复用此检查，编辑器在候选通过文件边界后才写入历史。
通用 JSON 可承载尚未通过这些业务约束的内容，不能代替领域采用入口。组件领域规则由资源层 component_validation_policy 表达，validate_component_description 显式调用。
场景/Prefab 文件及文档恢复递归检查普通组件与覆盖新增组件；相机参数、Mesh URI 与运行时适配器复用同一校验。
通用相机设置编解码不检查透视范围，使用方通过 camera_settings::valid() 或组件采用入口检查。
变换 setter 仍维持有效变换约束，场景保存另检查整体变换。
错误版本、重复格式头、超出文件大小及已移除字段由文件边界拒绝；普通对象没有这些文件约束。
导入器输出、编辑器保存和内置资源的 asset_decoder 默认路径使用文件入口。
未知文件类型仍可给 asset_manager 显式注册字节解码函数，不在 Meta 注册文件协议。

## 定义归属

错误值为 `serialization::diagnostic`，定义在 `diagnostic.hpp`；
内容编解码、引用表示和对象约束分别由 content_codec、reference_policy、object_policy、
validation_policy 表达，定义在 `policy.hpp`。Meta 仅保存这些扩展，不解释内容。
类型构造与继承转换属于 Meta 的 object_ops，不存放于编解码绑定。
内建反射对象根类型的注册实现在 `serialization/builtin_registration.cpp`；value 是内建 variant 包装，
无需登记自己的字段或 codec。JSON 节点适配位于 `detail/json_adapter.hpp`，
命名空间为 `serialization::json::detail`；普通调用方使用文本、字节或 reader/writer 入口。

## 类型分派与资源文件

JSON 与通用 reader 的返回值模板共用 `detail::decode_typed`：Meta 非对象类别通过临时实例解码，
对象类别通过注册的构造或解码能力创建，因此保留非默认构造对象支持。编码统一从 const_instance 遍历。
向量、四元数和矩阵共用固定长度分量读取，保留有限 float 校验和实际类型。

Mesh JSON 使用 `mesh_description` 注册字段，由 `asset_file` 处理 `gneiss.mesh` 版本 3 格式头；
字段反射读取后校验三角形列表、数组数量对应和法线归一化，再转换为 `mesh_data`。
独立二进制 Mesh 编解码保持专用布局。

## 结构化属性值

`core/value.hpp` 的 record、sequence、map、optional、variant 分支共享只读节点，构造时复制输入，
不暴露可写内容地址。修改需构造替换节点；旧值保持不变。资产身份保存类型 GUID 与 URI，
文档对象身份保存 document/object GUID；二者都不持有运行时对象。运行时 object 分支仍共享对象，
不能直接进入持久化文档；`validate_persistent_value` 检查嵌套引用、重复字段/键、非法键和深度。

结构化分支通过 Meta 属性注册和通用 variant 遍历编码，使用 `$kind/$content`。
record 按属性 ID 保存，map 按键比较，二者比较不依赖输入顺序；sequence 的顺序参与比较。

`serialization/value.hpp` 提供 `capture_value(context, const_instance)` 和
`apply_value(context, instance, value)`，直接在反射对象和内存数据之间转换，不经过 JSON。
支持对象、容器、optional、具名 variant 与内嵌多态引用。捕获拒绝循环引用，恢复可部分修改目标，
业务应在候选描述上恢复和校验后提交。有类型入口 `capture_value(context, object)` 与
`restore_value<T>(context, value)` 共用同一实现，后者创建独立候选，失败不返回部分对象。
多态引用按实际类型构造，再通过注册继承链转换到声明基类并共享其拥有者。捕获失败返回 diagnostic，恢复返回 diagnostic；
path 使用属性 ID、序列位置和映射键定位失败节点。资产引用通过注册的 capture_reference 保存身份，恢复由上下文解析，
不复制纹理、网格和材质内容。当前映射反射只支持字符串键；数据树可表达其他允许的稳定标量键。

Prefab 属性覆盖保存路径和 value_operation，操作内容使用 value；原 property_snapshot JSON 中转已删除。
稳定身份列表使用增删移动操作，普通列表整体 set。组件身份与不可变文档历史的实施范围见 [PLAN-104](../plans/PLAN-104-value-document-prefab-overrides.md)。

### 值树编辑

`apply_value_edits(root, edits)` 在候选根应用 set、insert、erase、move、put_entry、erase_entry；
失败返回操作索引和错误，不修改原根。`replay_value_edits` 保留冲突索引，继续应用后续操作。
这两个入口只校验持久化值结构，领域类型、层级和依赖约束由调用层在采用候选前校验。

序列值保存 fixed/keyed 能力和领域提供的 identities。固定序列允许索引路径；可变有身份序列
只允许 GUID 路径，增删移动也通过 GUID 指定元素和前置锚点。nil 锚点表示末尾，缺失锚点返回
not_found，已不存在元素的删除成功。普通可变列表只支持整体替换，不猜测元素身份。
`diff_values` 为记录、固定序列、稳定身份序列及映射生成编辑；其他变化整体 set。

引用策略只提供 encode 或 decode 时，若没有 capture，非空引用快照返回 unsupported，
不能回退为内嵌对象内容；空引用仍可捕获为空值。
