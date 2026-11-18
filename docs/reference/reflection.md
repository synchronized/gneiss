<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Type Registry 与反射元数据

## 当前范围

反射注册与查询使用 `engine/include/gneiss/engine/core/meta/registration.hpp` 和
`registry.hpp` 中的原生接口。Registry 的 C 函数、公共句柄表及 C 元数据缓存已删除。
旧 `reflection.h`、C 属性值布局和转换头已删除；Editor/Runtime 属性命令直接持有原生 `guid`。
IPC 使用 guid::string 的标准带连字符字符串编码 GUID，不传输原生对象布局。

当前支持类型、字段、显式属性访问器和显式单基类登记，内建 Transform 与 Camera 已接入。
类型构造和基类转换通过 object_ops 查询，序列化使用这些能力恢复对象；
不提供通用对象创建服务、注销、解冻、描述替换或动态模块卸载。

## 身份与注册

Meta 的类型与函数统一位于 `gneiss::meta`，不保留旧命名空间别名。
`type_definition.properties` 保存注册描述；查询返回保活注册表的 `type` 和 `property_range`。
数学属性直接使用 `gneiss::math::vector3`、`gneiss::math::quaternion`，不提供属性专用别名。
属性编号与存档、IPC 中既有的 `"field_id"` 格式键保持不变。

文件按职责组织，不新增第二套注册表：

| 文件 | 职责与主要类型 |
| --- | --- |
| `meta/type_types.hpp` | 类型分类与枚举项 |
| `meta/property.hpp` | 属性 ID、内存访问约束、注册描述与借用句柄 |
| `meta/type.hpp` | `type` 查询句柄 |
| `meta/type_definition.hpp` | 注册输入，组合对象能力与不透明扩展元数据 |
| `meta/ops/object_ops.hpp` | 构造、基类指针转换和反射对象转换 |
| `meta/registry_fwd.hpp` | 注册表前置声明与 `registry_ref` 所有者 |
| `meta/registry.hpp` | 创建/注册/冻结、属性访问及能力查询 |
| `meta/registry.cpp` | 唯一注册表的私有存储与实现 |
| `meta/registration.hpp` | C++ Builder 注册入口与模板身份适配 |
| `serialization/builtin_registration.cpp` | 基础值、数学类型及持久化策略的组合注册 |
| `serialization/diagnostic.hpp` | 编解码错误、路径与位置 |
| `serialization/policy.hpp` | 内容编解码、引用表示、对象及校验策略 |
| `serialization/json.hpp` | JSON 文本、字节与对象入口 |

Transform 类型身份直接使用 `math::transform::type_guid`；其属性标识位于
`engine/core/meta/transform_properties.hpp`，供数学类型注册和属性路径共用。
`guid`、`type_id`、`value` 仍是 Core 基础类型，`type_identity` 仍是 traits 定制点。

`gneiss::guid` 保存 16 字节稳定类型身份，全零无效；`gneiss::type_id` 是进程内类型编号，
不用于反射存档。字段完整身份由稳定 GUID 与非零字段编号组成。名称用于 UTF-8 显示和诊断。
GUID 字节顺序与规范 UUID 文本去掉连字符后的顺序一致，不按主机端序解释。

`property_definition.type_guid` 表示字段值的语义类型，无需事先注册该类型。
`property_access::read_only` 禁止为字段绑定 setter。

`create_registry()` 返回 `result<registry_ref>` 所有者；`register_type` 深拷贝名称和字段描述。
类型自身的稳定身份为 `type_definition.guid`、`type::guid()`；注册输入另有可选的运行期 `type_id`。
重复非空 GUID 或重复非空运行期 ID 的注册返回 `error::invalid_argument`，不覆盖已有记录。
普通 C++ 对象可仅注册运行期 type_id；GUID 与 type_id 不能同时为空。
多个无 GUID 类型通过 type_id 区分，nil GUID 的 find_type 查询返回空句柄。
注册名称不得为空或包含内嵌空字符，必须是合法 UTF-8；重复字段编号或名称也会被拒绝。

`bind_property` 绑定原生函数指针、类别和借用上下文；相同完整绑定可重复调用。
`freeze` 按 GUID 字节顺序和字段编号排序，之后禁止注册与绑定。重复冻结幂等成功。
独立表创建、字段绑定与冻结不吞掉 C++ 异常；冻结使用临时索引，失败或抛异常均不发布半成品。
冻结时同时建立包含继承属性的名称槽位索引，准备完成后统一提交；失败保留原注册内容，允许补齐后重试。
冻结后的已注册对象按 GUID 二分查询或运行期 ID 索引查询，属性按 ID/名称二分查询。
无 GUID 类型按进程类型 ID 排序，此顺序只用于当前进程枚举，不得持久化。
基础值、枚举及容器类型继续使用各自的既有解析路径，不承诺所有类型查找都已索引化。

无静态身份的外部类型可通过 `registration::type<T>().name("名称").guid(id)` 补充身份；
名称由注册器复制持有，再由 commit 深拷贝到注册表。与非空 traits 身份冲突时返回
invalid_argument。注册器只在显式 commit 时提交，重复 commit 返回 invalid_state。
字段仍使用 `.property<&T::member>(property_id, name)`，稳定编号不按字段顺序自动生成。

## 类型句柄查询

普通消费者通过 `find_type` / `type_of<T>` 获取保活注册表的 `meta::type`。
`name()` 借用注册对象名称；空句柄或没有注册名称的类型返回空字符串。
`type::find_property(property_id)` / `type::find_property(name)` 直接返回可判空的属性句柄，
缺少记录或属性时返回空句柄，不复制整份属性列表。
空类型、非对象类型和不存在的属性均返回空属性句柄；需要数据的实际读写操作继续返回错误。
属性句柄可继续查询类型、访问能力或通过 `get(instance)` 访问对象。
名称、属性范围与借用对象寿命分别遵循其所属句柄和实例的约束。

`type_count()` / `type_at(index)` 枚举已注册对象，保持 GUID 字节顺序；
`type::schema_version()` 返回注册版本，无注册版本时为零。
`find_property(type_guid, property_id)` 是获取同一属性句柄的便利入口，不生成元数据副本。

## C++ 类型注册

固定类型 GUID 推荐使用编译期字符串：

```cpp
static constexpr guid type_guid =
    guid::literal("00112233-4455-6677-8899-aabbccddeeff");
```

字面量格式错误在编译期拒绝。GUID 必须固定保存，重命名类型不能重新生成；
从字节数组迁移到字符串只改变写法，不改变持久化身份。

`type_identity<T>` 优先读取类内静态 `type_name` 和 `type_guid`，也允许外部特化；
未声明名称时回退到 `type_name<T>`，未声明 GUID 时为空。cv/ref 委托规范化类型。
当前 bool、float、vector3、quaternion 的 traits 提供内建字段原有的稳定 GUID；
普通内存类型可仅有运行期身份；需要持久化动态类型身份时须提供固定 GUID，不能按编译器名称临时生成。

`registration.hpp` 是普通 C++ 类型注册的主要入口，通过 `registration::type<T>()` 使用现有 Registry，
没有第二套运行期状态。动态描述仍可使用 `registry.hpp` 的非模板注册函数。
枚举使用 `registration::enumeration<E>()`；`enum` 是 C++ 关键字，不能作为函数名。
两个入口均可传入独立 `registry_ref`，省略时使用默认注册表，返回的 builder 实现位于 `detail`。
两种 builder 共享 `.name()`、`.guid()`、`.schema_version()`、`.metadata()`、`.extend()` 和 `.commit()`；
类型 builder 提供属性和基类注册，枚举 builder 提供 `.value(name, E)`。
枚举名称和 GUID 可从 traits 提取或显式补充；无名称、无 GUID 的枚举仍可登记显示项。
登记后的枚举类型支持 `type::name/guid/schema_version/extension` 及 `enum_capability::entries` 查询，
非空 GUID 可用于 `find_type`，并与普通类型共用唯一性约束。
枚举仍按整数读写，不新增逐枚举项 metadata，也不会自动以显示项约束值域。

调用序列化模板须直接包含 `serialization.hpp`。不保留旧头文件转发层或 get/set 属性入口。
类型具有静态身份时可这样注册：

```cpp
return gneiss::meta::registration::type<math::transform>()
    .schema_version(1)
    .property<&math::transform::translation>(meta::property_id{1}, "translation")
    .commit();
```

- `property<&T::member>(id, name)` 自动推导值类别和 GUID，生成直接读写回调。
- `property<Getter, Setter>(id, name)` 接受成员函数或有类型自由函数；getter 接收 const 对象，
  返回 value 可表达的值；setter 接收可变对象和值，返回 result 或 void。
- `readonly<Getter>(id, name)` 接受数据成员或 getter，只注册读取能力。
- 默认 schema_version 为 1；字段名称、非零字段 ID 显式提供，不能依赖注册顺序。
- builder 拥有字段名称，成员指针编译进回调，不保存对象和 user_data；需外部同步。
- commit 一次性登记类型及绑定，重复字段、身份冲突、冻结返回错误，不留下该类型。
- 注册写入、metadata 和 builder 的分配/扩展回调异常向调用方传播，不再转换为 out_of_memory/internal；
  显式注册应由宿主执行边界覆盖。纯身份设置、句柄构造等不分配操作仍保留 noexcept。
  builder 只允许提交一次；多个类型的注册不组成整体事务。
- 直接成员适用于无需额外约束的属性；带约束的 setter 必须先校验再修改。
  不支持的 value 类型在模板实例化时报错，不引入任意对象或偏移量访问。

底层 `register_type(registry, definition, bindings)` 的非空 bindings 必须与 properties 等长、顺序对应。
空 bindings 保留原有只登记描述、之后显式 bind_property 的入口。

## 查询与属性值

`type_count`、`type_at` 枚举冻结类型；`find_type`、`find_property` 返回句柄。
`find_type` 支持稳定 GUID 和运行期 `type_id` 两种入口，指向同一份记录。
`register_type<T>` 从 `type_identity<std::remove_cvref_t<T>>` 补齐名称、GUID 和运行期 ID；
显式名称或 GUID 与非空 traits 身份冲突时返回 `invalid_argument`。`type_of<T>` 解析规范化 C++ 类型并直接返回类型句柄。未注册类型仍提供原生能力；
通过 `registered()` 判断是否关联已发布注册数据，基础值无需重复登记。
模板去除顶层 cv/ref；注册 `void` 或传入不匹配的非空 `type_id` 返回 `invalid_argument`。
不对应 C++ 类型的描述保留空 `type_id`，仍可按 GUID 查询。运行期 ID 不进入文件或 IPC。
`property_definition` 的编号和内存访问约束分别使用 `property_id` 与 `property_access`；`bind_property` 接收强类型字段编号。
find_type/find_property 返回可判空句柄，type_of 提供未注册原生类型能力；枚举数量和按索引查询
仍返回 result，未冻结为 not_ready，越界为 not_found。属性范围不复制字段数组，
范围和句柄保活注册表；借用名称不能超过持有者寿命。

原生属性值支持布尔、int64、uint64、float、double、UTF-8 字符串、稳定 GUID、三维向量和四元数。
浮点与数学值必须有限，字符串必须为无内嵌空字符的合法 UTF-8，GUID 值允许 nil；类型或对象身份是否允许 nil，由对应注册/业务规则校验。
字段专属范围和四元数归一化由 setter 校验，失败不得留下部分修改。

回调、read/write 及组件接口统一使用 [gneiss::value](value.md)。
getter 输出 `value&`，字符串复制进值；setter 接收 `const value&`，不保留输入引用。
不再存在借用值和拥有值之间的转换。`read_property` 返回 `result<value>`，成功快照拥有数据，失败不交付部分值；
`write_property` 直接传递同一份 const 值。

未绑定字段或缺少对应方向的访问器返回 `unsupported`。getter 成功时必须返回登记的值类别，违反此约定触发断言；数值非有限或文本非法返回
`invalid_argument`，`read_property` 不交付属性快照。getter 显式失败保留原始错误码。getter/setter 抛出的异常（包括 bad_alloc）直接传播，
由执行边界按致命异常策略处理，不转换为普通属性错误。
基础 getter 接收 `const void* object`，setter 接收 `void* object`。对象仅在调用期间借用，Registry
不复制、不持有对象，也不解释 World/Entity 标识。调用方必须保证对象实际类型、寿命及线程访问合法。
空对象返回 `invalid_argument`；不支持运行期校验任意 void* 的实际类型。
setter 应先校验再修改，Core 不回滚第三方回调已经产生的副作用。Prefab 临时数据使用同一 setter，
不再提供独立的候选访问接口；跨字段联合校验由提交数据的调用方负责。

## 内建组件与生命周期

`serialization::register_builtin_types` 注册基础值以及 `math::transform`、`math::camera_settings`，
支持默认表和显式独立表，不冻结注册表；重复注册返回 `invalid_argument`。
数学注册实现集中在 `core/serialization/math_registration.cpp`，不依赖 World 或资源模块。
`camera_description` 等业务描述由资源内置注册负责，应用初始化依次注册基础类型及其序列化策略、资源类型。
稳定身份使用 `math::transform::type_guid`、`camera_description::type_guid`。

`function/scene/component_registration.hpp` 提供 `read_component_property`、`write_component_property`：显式接收 `world&`、
Entity、类型 GUID 和字段 ID。先通过 World 接口校验线程与实体并定位节点；普通组件通过同一
适配器捕获、验证和安装，基础反射只操作副本。Transform 保留节点变换入口，确保层级传播。
Editor、Runtime 和运行时 Prefab 覆盖使用这些组件入口；Core 不依赖 World。

Transform 的反射对象为 `math::transform`；Camera 直接使用 `camera_description`，包含
`math::camera_settings settings` 和标记为编辑只读的 `bool is_primary`，不再注册独立属性包装类型。
相机参数通过 `settings` 的嵌套属性访问；活动状态由 World 捕获，序列化仍保存它。
组件运行时属性编辑入口拒绝写入标记为 `property_edit_policy{.read_only = true}` 的字段；活动相机切换使用 World 专用接口。
Transform 字段为平移、旋转、缩放；Camera 的 `settings` 内含 `fov`、`near`、`far`。

Registry 使用原生共享引用管理，不经全局句柄表。独立表在最后一个引用释放时销毁；
默认表由引擎模块持有至进程退出，不随应用关闭清空。
场景服务和场景实例持有引用，Prefab 操作在调用期间借用。
元数据与回调上下文的使用必须在相应所有者销毁前结束；Registry 不拥有回调上下文。

初始化阶段由调用方串行注册和绑定；冻结后的查询与访问器查找可以并发调用。
目标对象的线程规则由访问器决定，内建 World 操作仅允许所属线程调用。

## 类型编解码

`serialization/policy.hpp` 提供独立的 content_codec、reference_policy、object_policy 和 validation_policy，
`serialization/diagnostic.hpp` 提供拥有错误位置文本的 `serialization::diagnostic`。能力与类型、字段一起登记到同一 Registry，不依赖资源层。
`registration::type<T>().extend(serialization::codec<Encode, Decode>)` 推导类型适配；任一函数可为 nullptr，
但至少具备一个方向。同一 builder 不允许重复设置。
每个类型当前只登记一组编解码函数，自定义文件格式标识及版本由 codec 自行校验，不提供任意 capability 或通用对象工厂。

`serialization::find_policies` 按运行期 type_id 查询冻结 Registry 中的扩展函数表，不登记重复的格式名称。
`serialization::json::encode_bytes<T>` 返回 `result<vector<byte>, serialization::diagnostic>`，`deserialize<T>` 返回
`result<T, serialization::diagnostic>`。成功结果拥有值，失败结果仅携带错误码、路径、字节偏移和消息。
`serialization::diagnostic.error` 保存预期失败的错误码；C++ 分配、访问器和 codec 异常直接传播。
JSON 适配层在 yyjson 返回分配失败后抛出 bad_alloc，不从 C 分配回调抛异常。
语法错误和无法编码的 JSON 值仍返回 invalid_argument；通用 reader/writer 自身返回的错误遵循其接口契约。
其他模块的分配失败分类仍在 PLAN-109 中迁移。
Builder 接收返回上述结果的 C++ codec；擦除后的解码回调返回 `result<ref<void>, serialization::diagnostic>`，
模板接口按已登记的运行期类型 ID 找到回调后移出具体对象，不要求默认构造或移动赋值。
未冻结返回 not_ready，未知类型返回 not_found，空 Registry 返回 invalid_handle。
缺少自定义方向时使用字段遍历；解码若既无自定义函数也无构造能力，返回 unsupported。
回调在 Registry 锁外执行，要求可并发处理不同对象；对象与输入只在调用期间借用。
输入大小限制由文件读取入口控制，反射不负责 I/O、缓存、任务或 GPU 上传。

资源层 `register_builtin_resource_types()` 统一登记材质、采样参数与枚举、ActionMap、Scene、Prefab
的类型及 codec，统一使用默认表。注册集中在 `resource/builtin_resource_registration.cpp`；
codec 文件只提供编解码入口。首次失败立即返回，不回滚已注册内容；重复注册失败。
组件描述随资源入口一起登记。冻结前依次登记 Meta 内置类型、World 基础值和资源类型；默认 initialize_meta 已按依赖顺序集中调用，
无需资源消费者重复注册。Serialization 的 register_builtin_types 提供默认表和显式表版本，独立注册表可据此读写 gneiss::value，无需加载资源模块。
材质直接注册 parameters、texture_uris 及嵌套字段，格式包装器只处理格式头；实际格式见[材质格式](render-asset-formats.md#material)。

## 反射 JSON 与对象校验

注册字段即参与共享遍历，不再需要启用开关。可默认构造的 C++ 类型自动登记构造能力。
已登记的自定义 encode/decode 按方向优先；缺少该方向函数时回退到字段遍历。

```cpp
// math::camera_settings 已独立注册 fov / near / far 和参数校验。
meta::registration::type<camera_description>()
    .property<&camera_description::settings>(
        meta::property_id{1}, "settings")
    (serialization::field_policy{.required = true})
    .property<&camera_description::is_primary>(
        meta::property_id{4}, "is_primary")
    (serialization::field_policy{.required = true})
    .commit();
```

属性名直接作为 JSON 字段名，支持 bool、int64、uint64、float、double、string、guid、
vector2、vector3、vector4、quaternion；数学值按分量数组保存，GUID 使用规范小写带连字符文本。
属性的稳定身份由 `type_identity` 提供。支持独立注册的嵌套值对象及[容器视图](reflection-containers.md)列出的容器；不支持指针图。
沿用已有属性访问边界：整数严格校验范围，浮点必须有限，字符串为无 NUL 的合法 UTF-8，
GUID 值不能为 nil。四元数的归一化等业务约束由类型校验器决定。

缺失普通字段保留 T 的默认值；`serialization::field_policy::required` 字段缺失返回 invalid_argument。
null 不等于缺失。只读属性不输出，也不从输入写入；read_only 与 required 不能组合，序列化入口返回带字段路径的 invalid_argument；Meta 注册不解释该格式策略。
未知字段忽略，重复 JSON 键拒绝；普通类型不会自动保留未知字段。
字段错误路径使用 JSON Pointer 转义，语法错误提供字节偏移。
沿用属性访问语义：getter 成功但数值/文本内容非法返回 invalid_argument；显式失败保留原始错误码和字段路径。

`.extend(serialization::validation<Fn>)` 接受返回 error 或 serialization::diagnostic 的只读校验函数。
成功诊断的 error 为 success。编码前、解码填充结束后各校验一次，适用于自定义 codec 与反射 JSON。
解码在独立临时对象中写属性，失败析构临时对象；访问器不得产生需要回滚的外部副作用。
校验器和 codec 异常直接传播；临时对象和序列化操作上下文通过 RAII 清理，不承诺回滚回调的外部副作用。

类型擦除入口 `serialize_object(registry, type_id, object)` /
`deserialize_object(registry, type_id, bytes)` 与模板入口共用分派；对象指针必须匹配类型。
不要直接调用查询到的 codec 函数指针来绕过校验或反射回退。
Meta 核心的显式重载使用传入的冻结 Registry；无参重载使用默认表。
业务资源、组件描述及 CPU 解码适配器统一使用默认表，不再保存或接收注册表引用。

Camera / MeshRenderer 的字段已改为反射注册并标记必填，仅保留透视参数和资产 URI 的业务校验，
Scene / Prefab 的结构编解码继续使用专用实现。

## 注册表创建与状态查询

隔离测试可用 Meta 核心 `create_registry()` 创建空表，自行注册测试类型。
`is_frozen(registry)` 返回 `result<bool>`，`type_count(registry)` 返回 `result<std::uint32_t>`；
无效注册表返回 invalid_handle，未冻结注册表的类型数量查询返回 not_ready。
成功的 false 或零数量不代表操作失败。
默认表首次创建、查询锁和自定义类型能力查询产生的 C++ 异常向外传播，不伪装为查询未命中。
`type_of`、`find_type`、`property.type()`、`element_type()`、`dynamic_type()` 与实例创建不承诺 noexcept；
直接读取既有句柄的名称、身份、属性范围等操作仍保持不抛异常。

## 元数据与属性值查询

`type_of<T>`、`find_type` 直接返回 `type`；运行期查询未命中或注册表未发布时返回空句柄。
`type_at` 暂时返回 `result<type>`；
注册表级与类型句柄上的 `find_property` 均返回可判空的 `property`；
无冻结记录、未知类型或未知属性返回空句柄。属性读取和写入另行报告操作失败。
`serialization::find_policies` 返回 `result<serialization::type_policies>`。
句柄保活冻结 Registry；名称借用该表，不保活业务对象。
`read_property` 返回 `result<value>`；成功快照拥有字符串等数据，可在 Registry 释放后使用。
getter 显式失败或数值/文本内容非法时返回错误；成功却返回错误类别属于适配器契约违例，触发断言。
抛出的异常直接传播，不交付部分属性快照。
写属性仍返回 error；编解码与序列化模板返回 result。

## 无 GUID 对象边界

`registration::type<T>().name("名称")` 可以注册没有持久 GUID 的 C++ 对象；名称已有 traits 时可省略。
`type_of<T>`、view、member、getter/setter、嵌套对象按运行期 type_id 访问，
不能通过 nil GUID 查找这些类型。普通静态类型 JSON 内容不携带身份，可正常编解码。
多态 `$type_guid/$content` 必须有非空 GUID，否则编码返回 unsupported。
对象 value 快照使用稳定 GUID 标识结构，capture/apply 对无 GUID 对象返回 unsupported；
不把进程 type_id 写入文件，也不把所有 nil GUID 对象视为同一种快照。

## 默认注册表与启动

`meta::default_registry()` 返回进程共享的 `result<registry_ref>`，只创建表，不登记内置类型、不冻结。
定义位于 engine 编译单元；各模块应链接同一引擎，不能把引擎静态复制到多个动态库后期待共享状态。
未冻结时查询返回 `not_ready`。`meta::registration::type<T>()`、`register_type<T>(definition)`、
`type_of<T>()`、属性访问和 `serialization::json::encode(object)` / `serialization::json::decode<T>(bytes)` 使用默认表；
显式 registry 重载保持原行为。

业务模块不得直接调用 `meta::default_registry()`；默认表获取集中在 Meta 内部，
普通调用使用无 registry 的查询、注册、序列化及视图入口。
`const_instance::from_object(type_id, address)` 和可写版本使用默认表，错误按 result 返回。
`serialize_instance(view)`、`deserialize_instance(view, bytes)` 及内部 DOM 适配使用
视图自身绑定的注册表，不重新获取默认表，因此独立表的字段与 codec 语义保持不变。
Meta 内部递归与 codec 回调仍保留显式上下文；注册表机制测试允许直接验证默认表。

`engine/function/meta_registration.hpp` 的 `gneiss::initialize_meta(callback = nullptr)` 在启动阶段
串行登记内置 World 属性、资源 codec、项目类型，再冻结。callback 不带参数，
可直接使用默认 builder。需要自定义类型的宿主在首次 `application::create` 前调用带 callback 的入口；
application 自动调用无参入口，重复无参调用返回首次结果。独立工具应显式完成初始化。
初始化期间不得并行修改或手动冻结默认表，callback 不得重入初始化函数。

首次注册返回预期错误会保留失败结果，表不冻结；后续无参调用返回原错误。
初始化回调的意外异常向宿主执行边界传播，由自有宿主致命处理，不作为可恢复初始化失败继续运行。
已启动后再提供 callback 返回 `invalid_state`。不支持运行期扩充、重置默认表或插件热卸载。
独立 registry 仍可通过 `create_registry()` 创建并按原接口注册、冻结，不受默认表状态影响。

`type::id()` 提供已注册 C++ 类型的进程内身份，供 GUID 分派后进行类型检查；不会持久化。
默认启动也注册 Camera、MeshRenderer 的描述类型与 codec，文件协议见[场景格式](scene-format.md)。

## 嵌套值对象

成员绑定 `.property<&T::member>()` 可指向独立注册的 C++ 值对象。
`property::type().id()` 为成员的运行期类型 ID，`type.guid()` 为其稳定身份，
`type.kind()` 为 object；它不占用通用 value 的新类别。冻结时检查子类型已登记且两种身份匹配，
缺失子类型返回 not_found，身份不匹配返回 invalid_argument。登记顺序不限。

`find_object_property` 查询借用成员访问器；地址仅在父对象存活且地址稳定时有效。
可写访问用于解码中的临时对象，禁止与父对象的其他写操作并发。
普通 `read_property/write_property` 不对嵌套对象作字节复制，返回 unsupported。
自动成员绑定要求值对象可移动赋值；不提供嵌套 getter/setter；直接成员可标记 read_only。

反射 JSON 对同一文档递归读写，根深度为零，最多允许 64 层嵌套成员；超限返回 invalid_argument。
子类型自定义 codec 按方向优先，但必须使用 JSON 对象内容；二进制 codec 不适合作为嵌套 JSON 属性。
自定义解码返回独立子对象，验证后移入父成员；反射解码直接填充临时父对象中的成员。

缺失非必填对象时保留父对象的成员默认值，并执行子类型校验；提供对象时，子类型的必填字段规则生效。
子类型校验失败的路径包含完整父路径，如 `/settings/near`；子 codec 的片段字节偏移不冒充整份文档偏移。
任一步失败均释放临时父对象，调用方不会接收到半成品。访问器和校验器不得依赖外部回滚。

相机描述的 C++ 数据为 `camera_description { math::camera_settings settings; bool is_primary; }`。
`camera_settings` 的身份通过外部 traits 定义，数学属性由 Serialization 内置组合注册；透视参数校验由资源组件领域入口和 World 采用时执行，数学类型本身不依赖 Meta。
Camera 直接绑定 settings 和 is_primary，文件内容为：

```json
{"settings":{"fov":1.0,"near":0.1,"far":100.0},"is_primary":false}
```

未发布的原扁平相机描述不再读取；仓库资产与生成脚本同步更新。
World 运行期属性与 IPC 仍保留既有名称，这些协议不等于资产文件中的嵌套结构。

## 属性句柄与范围

`type::properties()` 与只读实例的 `properties()` 直接返回 `property_range`。
未注册、无效或非对象类型返回空范围；空范围不等于已注册空对象，通过 `registered()` 区分。
序列化、快照和编辑等操作在需要注册数据时显式检查，仍返回操作错误。
范围和迭代器保活冻结注册表，直接访问冻结属性数组，不为每次遍历复制属性列表。
范围只读；下标访问要求 index 小于 size，end 不能解引用。

```cpp
auto fields = root->properties();
for (auto property : fields) {
  auto member = property.get(*root);
  if (!member) return member.error();
  // property.name()/type()/capabilities() 查询同一份冻结定义。
}
```

`property::get` 校验注册表及字段声明类型；即使属性 ID 相同，也不能用于不相关类型。
派生对象上的继承属性继续通过实例访问完成基类地址调整。
句柄不持有目标对象；只读访问、getter 临时值与可写访问规则同下述实例视图。
实例现命名为只读 const_instance、可写 instance；旧 mutable_instance 名称与静态 from 模板工厂已移除。
元数据查询统一返回 type/property 句柄，不再提供复制信息结构的查询接口。

## 实例视图

`meta/instance.hpp` 提供 `basic_instance<Mutable>`；只读 `const_instance` 与可写 `instance`
分别是 false/true 特化。两者共用类型句柄及访问位置布局，不新增对象所有者或注册表。
写操作仅在 Mutable 为 true 时可用；as_const() 借用原位置，不复制业务对象。
根视图通过 `meta::view(registry, object)` 创建，也可省略 registry 使用默认表；
可写对象返回 result<instance>，const 对象返回 result<const_instance>，需要主动只读时传 std::as_const(object)；仅接受实际对象的左值，
不接受临时对象。模板 view 可访问未注册类型的原生能力；属性访问需要已发布的注册数据。
运行期 from_object 入口要求已注册对象类型和冻结注册表。

```cpp
auto root = meta::view(registry, std::as_const(camera));
if (!root) return root.error();
auto settings = root->member("settings");
if (!settings) return settings.error();
auto near = settings->member("near");
if (!near) return near.error();
auto snapshot = near->read_value(); // result<value>，拥有读取结果
```

`member(name)` 和 `member(property_id)` 返回同一种视图。对象支持 `properties()` 枚举，
`read_value()` 要求有效且仍存活的借用实例，默认空实例读取触发断言。
已绑定对象访问器必须返回成员存储地址；空强引用仍是合法值，不等于空成员地址。
读取继续返回 `result<value>`，保留 getter 失败、内容校验和不支持该能力的诊断。
基础属性支持 `read_value()`；`kind()` 区分 invalid、empty、scalar、object、sequence、associative、optional、reference、variant；
`scalar_kind()` 区分基础值类别。`guid()` 返回静态稳定身份，容器通常为空；
`type_id()` 为对象、容器和容器元素提供运行期身份，普通标量属性仍通过 value_kind/GUID 标识。

基础属性视图保存父对象和属性位置，读取时调用原 getter，返回自有 value。
即使 getter 返回临时字符串，也不会借用该临时值；已经读取的快照不随父对象变化。
再次 read_value 则读取父对象当前值。基础属性的 object_data 返回 unsupported。
对象视图的 object_data 返回借用的对象地址，不复制嵌套对象。

`meta::view(T&)` 返回可写视图；`as_const()` 转为只读视图，不提供反向转换。
assign 的目标实例、实例复制的来源实例以及 replace 的目标必须有效且借用存活；
这些写操作使用默认空实例时触发契约断言。类型不匹配、非法标量内容、能力缺失和 setter 拒绝仍返回错误。
replace 在独立候选中初始化，初始化失败直接返回其错误且不调用 setter；成功才提交给 setter。
属性候选构造器必须返回非空对象，分配异常向执行边界传播，不以空指针伪装 out_of_memory。

可写 member 拒绝只读/无 setter 的属性，通过 assign 执行原 setter 的校验与错误语义。
只读属性仍可通过 as_const().member 读取。单字段编辑不自动执行整对象跨字段校验，业务方应在
完成编辑后执行对应验证。嵌套自定义 codec 的整体赋值通过 assign_object 接入，先校验类型 ID，
成功后移动源内容；该接口不提供失败回滚，不应用于承诺原子编辑的业务对象。

视图持有 Registry 以维持元数据寿命，但不持有根对象。父对象销毁、移动或内部地址失效后，
相关视图不可再使用。子视图不依赖根视图变量自身的寿命。共享视图不意味着目标对象线程安全，
修改与读取仍需调用方同步。from_object 是内部类型擦除入口，调用方必须保证实际对象匹配类型 ID。

现有反射 JSON 的属性枚举、基础值读写、嵌套遍历和子 codec 赋值均通过实例视图完成。
底层 find_object_property 仍用于实现成员地址访问；序列化器不再直接操作这些回调。

容器注册、访问与 JSON 规则见[反射容器视图](reflection-containers.md)。


## 原生描述与文件包装

材质的 parameters、texture_uris 以及 Scene/Prefab 的普通对象字段直接注册原生成员。
Scene/Prefab 不缓存来源版本或派生依赖列表，依赖由组件描述按需收集。
组件动态分派使用通用引用反射，Prefab 组件覆盖按成员反射，属性覆盖按 key/value 成员反射；保存直接从描述生成文件，
不合并旧 JSON 或保留未知扩展字段。Scene 的 parent 键映射原生 object_address，object/source 均通过 GUID 的通用规则编解码；具体见场景格式参考。

`.metadata(serialization::object_policy{.reject_unknown_fields = true})` 启用严格字段检查；默认 false 保留忽略未知字段行为，两者都拒绝重复键。
特殊类型通过通用 reader/writer 操作注册，签名和上下文规则见[序列化接口](serialization.md)。
普通类型不注册格式专用 codec；资源文件包装在 asset_file 入口执行。
内部 JSON 适配头为 serialization/detail/json_adapter.hpp，IPC 使用 read/write 或 read_value/write_value。
value 的标签与引用内容由通用 variant 和引用遍历处理，JSON 仅适配基础节点。
材质文件包装仅处理 format/version，直接读写 v6 原生描述，不再创建文件数据副本类型。
材质、Scene/Prefab 及其嵌套组件使用已初始化并冻结的默认表；业务注册入口不接受独立表。

原生窄整数和枚举通过现有 int64/uint64 标量访问，写入先检查原生底层范围；
枚举定义域由所属类型 validate 校验。它们的属性 GUID 表示规范化后的整数存储类型。
vector4 使用独立 vec4 标量身份和四分量 JSON 数组；旧 IPC 的四元数类型不承载 vector4。

### 资源文件外层包装

Material、Texture、Action Map、Scene 与 Prefab 使用 resource/asset_file.hpp 共用文件包装：
验证唯一的 format/version、对象根与配置的字节上限，内容交给反射字段序列化。
编码拒绝反射字段占用 format/version 名称，并在添加格式头后检查完整输出大小。
Material 保持版本 6，非零未知版本返回 unsupported；Action Map 保持版本 1 与 1 MiB 上限，
版本为非零未知整数时返回 unsupported，缺失或非法版本返回 invalid_argument。业务字段校验仍由各类型的 validate 完成。

vector2 使用独立 vec2 身份及 `[x,y]` 数组，严格检查两个有限 float 分量。
value_kind 使用显式分组编号，与 variant 下标无关。Prefab 与 IPC 使用通用 $kind/$content，
vec2、vec4 和 quaternion 保持独立分支，矩阵按列优先数组保存。

## 类型视图

`meta::const_instance::type()`（包括可写实例）返回其持有的轻量 `meta::type`，不重新查询注册表。
实例的类型身份、类别和原生能力统一由此句柄提供，不重复缓存；冻结后需要新注册数据时重新创建视图。
视图持有原注册表，
不拥有业务对象；`kind()` 查询结构类别，`scalar_kind()` 查询 value 的承载类别，
`is_enum()` 区分 C++ 枚举与整数，`id()` 保留枚举自己的运行期类型身份。
枚举 GUID 来自自身注册数据；未登记 GUID 时为空，不冒用整数 GUID。

`type.capability<enum_capability>().entries()` 直接返回只读 span，借用冻结注册表中的枚举项；没有注册记录、非枚举或
无效类型返回空范围。通过 `is_enum()`、`registered()` 和类型句柄的 bool 判断区分这些状态。
借用结果不得超过能力句柄或注册表的生命周期。`properties()` 返回保活注册表的属性范围。
元素类型通过对应容器 capability 查询；调用 element_type() 要求能力有效。
模板注册会将静态原生能力关联到注册记录，按 ID/GUID 查询或 type_at 枚举时保留这些能力。
纯运行期定义可不关联原生能力，不会因此获得容器或引用操作。
类型句柄的相等比较包含注册表身份：有运行期 ID 时比较 ID，否则比较 GUID；
两个空句柄相等。跨表判断同一种 C++ 类型或持久类型时，显式比较 id() 或 guid()。

属性面板的 `draw_property_value` 接收类型视图和 value，检查存储类别匹配后从类型查询
枚举选项。枚举容器元素也走此路径；注册表无需为每个属性复制枚举列表。
property 句柄通过 type() 返回属性值类型，不重复保存
kind/category/object_type/enum_type/choices/type_guid。属性提供 id、name 和 capabilities 查询。
属性类型持有原注册表，不新增注册表或循环所有权。

### 不依赖实例的类型查询

包含 `registry.hpp` 后可以调用 `meta::type_of<T>()`、`meta::find_type(type_id)` 和
`meta::find_type(guid)`；每个入口也支持显式 registry_ref。查询要求注册表冻结，
未冻结返回 not_ready，空注册表返回 invalid_handle，零或未知身份返回 not_found。

内建标量复用 value 的静态类型集合，无需注册。C++ 对象和枚举必须在当前注册表登记；
模板查询规范化 const/ref，并检查容器叶子类型。按 ID 查询可发现已注册属性引用的容器
及其嵌套元素；查询不会把任意容器隐式加入全局表。容器没有独立稳定 GUID，枚举仍使用
运行期身份；GUID 查询不把枚举当成其底层整数。窄整数的 ID 保留 C++ 类型身份，其 GUID
沿用现有整数存储类别，因此 GUID 查询返回规范的 64 位承载类型。

面板和 JSON 序列化通过类型视图判断结构和标量类别；具体读写仍由 const_instance 完成。
包含自定义 getter/setter 的属性不会因新增类型操作表而绕过访问器。

## 对象所有权

`engine/core/ref.hpp` 提供 `ref<T>` 与 `make_ref<T>`，不要求类型继承共同基类。
资源层 `asset_ref<T>` 是它的语义别名。反射解码对象、组件描述的不可变内容以及编辑器
整对象编辑载荷共用该拥有者；复制保留同一控制块，转换为 const/void 不改变删除器。

`make_ref` 创建普通 CPU 对象；需要线程限定销毁的资源继续使用资源工厂的自定义删除器。
`get()` 和 `meta::const_instance` 是借用入口，调用方须保留拥有者，不能由借用指针重新创建所有权。
共享引用不保证对象数据线程安全，引用环须由弱引用或对象 ID 打破。

引用属性的持久化支持范围见下节；不代表任意共享对象图均可自动编解码。
资源标识、场景 GUID 和内嵌组件描述仍按各自已有格式处理。设计边界见
[ADR-098](../decisions/ADR-098-object-reference-ownership.md)。

### 引用属性访问

注册 `ref<T>` 成员后，`property.type.kind()` 为 `type_kind::reference`，
`reference_capability::element_type()` 返回静态目标类型；目标对象类型仍需注册。ref<void> 不支持字段反射。

通过 `const_instance::as_reference()` 获取 `reference_view`，再用 `get()` 获取只读借用目标。
`instance::as_reference()` 返回可写引用视图，get 仅对 ref<T> 的可写目标有效；
ref<const T> 返回 unsupported，可通过引用视图的 as_const().get() 读取。
has_value 检查是否非空；视图本身不持有目标强引用。
reference_view::get 对空引用返回 not_found。引用不是标量；可变 meta::object 目标支持 value 强引用传递，其他引用的 read_value 返回 unsupported。

`mutable_reference_view::assign(source)` 接受只读引用视图，对完全相同的 ref 类型共享控制块，不复制目标；
不同目标类型（包括 const 差异）返回 invalid_argument，不隐式推断派生转换。
引用视图的 `reset()` 返回 void，释放当前引用，不影响其他强引用；视图不会自动延长目标寿命，
重置/替换后应重新获取目标视图。
`reference_view::has_value()` 直接返回 bool；`owner()` 直接返回 `ref<const void>`，
空引用对应空强引用，非空结果显式保活目标；`actual_type()` 查询实际运行类型。
可写视图的 `assign_owner(id, owner)` 仅接受声明目标类型的 ID，调用方须保证擦除指针与 ID 匹配。
修改目标会被所有共享拥有者观察到，不提供写时复制。
实例的 `registry()` 返回所持有的注册表；受 setter 约束的复合属性通过
`requires_replacement()` 判断整体替换要求，`replace(initialize)` 在独立候选上初始化，
成功后才调用 setter。普通成员继续使用 `assign`，序列化不访问 Meta 私有状态。

默认反射 JSON 按值内联引用：空引用输出 null，普通 ref<T> 输出目标字段。
声明类型通过 registration::type<T>().polymorphic() 显式启用多态后，输出
`{"$type_guid":"实际类型 GUID","$content":{...}}`。具体类型通过 base<Base>() 注册
安全基类转换，未知/无关类型在构造前拒绝；未声明多态的引用不能保存派生对象。
具体类型可使用反射字段或已有自定义 JSON codec，后者仍需输出 JSON 对象。

多态对象继承 meta::object（object.hpp），具体类可用 object_type<Derived, Base>
（object_type.hpp）提供静态类型能力与完整对象地址。对象不保存注册表或类型句柄。
dynamic_type() 返回 meta::type；带 registry_ref 的重载使用指定注册表。
未注册对象仍返回有效的静态类型，registered() 为 false；没有静态描述时返回空句柄。
持久身份使用 dynamic_type().guid()，不再提供独立 dynamic_guid()。
引用视图使用自己的注册表解析动态类型，多继承引用仍通过 dynamic_address() 取得完整对象地址。
reference_view.get() 返回实际类型视图；ref<const T> 只允许读取目标。
base 链同时用于身份校验、指针调整和继承属性访问。freeze 合并完整继承链的属性，
按属性 ID 排序；任意祖先与派生类的属性 ID 或名称冲突返回 invalid_argument，不提供遮蔽。
基类尚未注册返回 not_found；冻结失败不会留下部分合并结果，可补齐注册后重试。
property::declaring_type() 提供声明类型的可选 GUID，declaring_type_id() 提供运行期身份；
原生实例用运行期身份定位父对象及继承字段，const_instance 和 read_property/write_property
自动按注册的 upcast 链调整基类地址，支持基类在完整 C++ 对象中具有非零偏移。
底层 find_object_property 返回声明类型的原始访问器，调用回调时须传入声明类型的地址；
普通调用方使用 const_instance。继承字段同样参与 JSON、value 捕获/恢复，遵守 required、
read_only 与 setter 校验，不需要在每个派生类重复注册。

重复引用独立保存并恢复为不同对象，不保持内存共享身份。递归活动路径检测循环并报错，
最多 64 层嵌套；不生成自动 `$root` / `$objects` / `$ref` 对象表。
需要持久化共享身份的业务显式保存对象列表及 object_id；外部资产继续使用资源 URI。
自定义 codec 自行调用序列化会建立自己的上下文，须自行避免跨 codec 的递归环。
设计边界见 [ADR-103](../decisions/ADR-103-inline-polymorphic-references.md)。

### 跨进程组件描述

Meta 注册表仅属于当前进程。通过 IPC 接收场景组件描述的宿主，也必须在解码前完成
`initialize_meta`；启动的 Runtime 子进程初始化不能替代 Editor 接收端初始化。
独立运行的 IPC 集成测试同样需要遵循此启动约定。协议解码本身不隐式初始化或冻结注册表。

### GUID 的 JSON 表达

按反射类型解码：GUID 的 null 恢复为 guid{}，规范 UUID 字符串按 GUID 解析，其余 JSON 类型拒绝。
非 nil GUID 保存为字符串，nil 保存为 null；全零 UUID 字符串仍可解析，重新保存统一为 null。
字段缺失沿用 required/默认值规则，不等同于显式 null；非空身份约束仍由业务校验负责。
此规则不依赖 optional 反射，也不改变 ref 的所有权和空引用规则。

### 枚举名称的 JSON 表达

字段扩展 `serialization::field_policy{.enum_name = true}` 使 JSON 使用枚举注册项的名称。C++ 属性保持枚举类型，
类型视图仍可查询枚举列表，`read_value()` 仍返回底层 int64/uint64。未标记属性保持数字编码。
名称编码拒绝未注册值；解码只接受已注册名称，拒绝数字、null 和未知名称，诊断定位到属性路径。
该标记用于枚举属性，不是任意容器或对象的格式转换选项。

Texture 描述注册 source、color_space、usage 三个必填字段；后两者使用 enum_name。
文件保持 gneiss.texture v2、字符串枚举及 normal 必须配 linear 的校验。调用描述 codec 前
需初始化默认 Meta 注册表；不会自动启动 Application 或 GPU。

### 非标量 getter/setter 属性

`registration::type<T>().property<Getter, Setter>(id, name)` 支持嵌套对象、序列、关联容器和引用。
非标量 getter 必须返回稳定的 const U&，不接受临时值；setter 接收完整 const U&，返回 error
或 void，负责先校验再修改，失败须保持旧属性。未知 U 仍需按原规则注册。

此绑定的 instance 支持 assign/assign_object 整项替换，但不提供可写对象地址、
可写容器/引用视图或可写子属性；通过 as_const() 读取嵌套内容。属性替换后重新获取借用视图。
反序列化先创建独立默认候选并完成解码，然后调用 setter；解析失败不调用 setter。
setter 返回的业务错误定位到属性，嵌套解码错误保留具体子路径。
这只保证单个受约束属性的替换边界，不承诺整个父对象或任意回调副作用的自动回滚。


## 可选值反射

`std::optional<T>` 对应独立 `type_kind::optional` 和 `optional_ops`，不改变值所有权，
也不引入共享引用。`type.capability<optional_capability>().element_type()` 返回 T 的类型。
通过 `const_instance::as_optional()` 获取只读借用视图，提供直接返回 bool 的 `has_value()` 和保留失败诊断的 `get()`；
可写视图增加 `ensure()` 和 `reset()`。空值的 `get()` 返回 not_found；T 无默认构造时，
空槽位的 ensure 返回 unsupported。清空、替换或移动宿主后须重新获取内部视图。
optional 本身不支持 read_value，须先 get 后读取内部值。

JSON 空 optional 编码为 null，有值直接编码 T；缺失字段保留默认构造值，显式 null 清空。
内部有值却编码为 null 时返回 unsupported，避免与空 optional 混淆，例如含 nil GUID、
空 ref 或空内层 optional。普通嵌套对象和容器沿用现有遍历与校验。
非标量 getter/setter 属性只开放只读 optional 视图，解码走临时候选和整项 setter，
不能借 ensure/reset 绕过 setter 校验。此能力不自动为编辑器增加可选值控件。

Variant 的标签与视图规则见[Variant 分支反射](reflection-containers.md#variant-分支反射)。

## Value 对象引用

对象分支的共享、比较和持久化规则见[通用值](value.md#对象强引用)。

const_instance::read_value 对可变目标的 ref<T>（T 派生自 meta::object）返回共享强引用；
instance::assign 对可直接访问的引用槽接受兼容对象或空引用，错误类型不改变旧引用。
需要 setter 整项替换的属性仍使用 assign，避免绕过业务校验。
这些属性仍为 reference 类别，不变成 scalar。ref<const T> 不转换为可变对象引用；
普通结构体继续使用 const_instance，const 引用仍可通过 reference_view 访问。

原生 value 槽位也支持 const_instance::read_value 与 instance::assign；复制对象分支仍共享引用，不意味着生成快照。

## 序列化格式边界

序列化实现位于 `engine/core/serialization/json.cpp`，普通调用包含 `serialization/json.hpp`。
`serialization::json::encode` 返回 JSON 文本，`encode_bytes` 返回相同 JSON 的字节；
`decode<T>` 接受文本或字节。两种入口均提供 context-first 和默认上下文版本。
Meta 不再提供含糊的 serialize/deserialize 入口。

注册表保存构造、继承和不可变扩展元数据；Serialization 从扩展解释编解码与校验策略。
字段遍历默认启用，`.extend(serialization::codec<Encode, Decode>)` 提供特殊类型的通用操作。
资源二进制协议通过 asset_decoder 显式注入解码函数。

字段遍历已独立于 JSON 后端，接口与限制见 [序列化](serialization.md)。

## 类型能力与序列化绑定

`type_definition.object` 保存 `meta::object_ops`，通过 `find_object_ops` 查询。
注册表冻结前返回 not_ready，未知类型返回 not_found，空注册表返回 invalid_handle。
已登记但没有相应能力的类型返回空函数项；调用前须检查函数项。
各序列化策略分别存在类型扩展元数据中，通过
`serialization::find_policies` 查询；`type_definition` 不定义专用序列化字段。
两者由同一注册表保存；继承关系不自动启用多态保存策略，校验时机保持不变。

### 类型与属性扩展元数据

`registration::type<T>().metadata(value)` 为类型登记扩展；
`property(...)(extension, ...)` 或 `readonly(...)(extension, ...)` 为当前属性登记扩展。
property 返回借用注册器及字段索引的局部代理；后续字段扩容不影响索引，
注册器移动或销毁后代理失效。类型 metadata/extend 与字段 operator() 的作用域分开，
不附加扩展时可直接继续 property 或 commit；代理销毁不隐式提交。
扩展以其 C++ 类型为键保存独立的只读值，同类重复登记返回 invalid_argument，
预期失败在 commit 返回，不登记半个类型；扩展构造或工厂抛出的异常直接传播。
`type::extension<E>()` 和 `property::extension<E>()` 返回借用指针，缺失时为 nullptr。
句柄保活元数据；元数据结构中的借用指针仍由提供者负责寿命。
Meta 不解释扩展内容；其用途及策略优先级由消费模块定义。

## 注册名称的存储与借用

注册表内部的类型名、属性名和枚举项文本由 `string_name` 字符串池共享。
注册 builder 在提交前拥有临时字符串；校验和重复注册检查通过后才将名称入池。
普通失败注册不增加名称条目；内存分配异常不承诺回滚已经入池的文本。

`type::name()`、`property::name()` 仍返回 `std::string_view`。
`enum_entry::name` 为文本视图：手工注册输入须保持至注册调用返回；builder 的 `.value()` 会复制输入。
查询得到的名称借用池内不可变文本至 Engine 卸载，枚举项集合本身仍借用所属注册表。
枚举查询名称为完整的 NUL 结尾文本，空名称的 `data()` 可以为空。

名称排序仍按文本；按名称查询不会将未知文本加入字符串池。
序列化仍输出名称文本，不保存指针或运行期哈希。
条目不单独回收，因此对象名称、URI、用户输入等动态字符串不自动迁移到此池。

### 能力查询与适配器前置条件

`member()`、`as_sequence/as_associative/as_optional/as_reference/as_variant()` 和
`property::get()` 是检查式入口：空实例/空属性返回 invalid_state，能力缺失返回 unsupported，
成员缺失返回 not_found；跨注册表或不匹配的属性身份仍返回 invalid_argument。
它们用于从动态类型逐步取得借用，与要求有效借用的 read_value/assign/replace 区分。

内部适配器在索引、存在性及能力检查通过后必须返回有效存储；返回空地址属于契约违例，
由实例构造入口断言。外部类型擦除入口 from_object 仍验证空地址、注册状态和对象类型。
借用失效不一定能被检测，调用方仍须遵守容器修改、引用替换及对象销毁后的有效期约定。

## 整体值转换与结构类别

`value_ops` 是独立于结构类别的能力，`value_traits<T>` 可为对象提供 `read(const T&) -> value`
和 `write(T&, const value&) -> error`。特化需在首次查询该类型前可见，并声明 `enabled = true`。
自定义写入应在失败时保持目标不变。内置标量自动提供转换。

`instance::read_value()` 与 `assign(value)` 使用这项能力；没有适配的对象仍返回 `unsupported`。
通过 setter 修改的对象属性在独立候选上转换，成功后调用 setter，保留其业务校验。
直接访问使用借用对象，整体读取返回值；视图保活 registry，但不延长目标对象生命周期。

提供整体转换不会把对象改为标量，也不会替代默认 JSON 字段遍历。`aabb3` 尚未增加原生
value 分支；本能力不等于任意 C++ 类型均可存入 value。

文件表示的显式适配见[序列化接口](serialization.md#值类型的文件表示适配)，与 value_traits 独立。

## 静态类型表示

representation_traits<T> 与 value_traits<T> 分别提供原生表示转换和整体 value 转换，
都是独立于注册数据的可选能力。type::capability<representation_capability>() 返回可判空的能力；
其 encode(source, consume) 与 decode(read, consume) 同步访问表示和候选临时值，
借用只在回调中有效。具体约束及用法见[序列化参考](serialization.md#值类型的文件表示适配)。

## 类型能力与实例视图

`meta::type_ops` 位于 `ops/type_ops.hpp`，与各项 ops 同属 meta；静态生成器 `detail::ops_of<T>()`
及内部遍历辅助仍属 detail。普通调用方使用视图，不直接操作函数指针表。

`type::capability<sequence_capability>()` 返回可判空的 `sequence_capability`，提供 `is_fixed_size()`、
`has_identity()` 和 `element_type()`；无需构造容器实例，因此也可用于 Prefab 路径校验。
`instance::as_sequence()` 仍返回实例视图，负责实际容器的元素访问和修改。
capability 保活类型上下文，不保活对象；类型专属查询和表示转换要求有效能力，先检查 bool。
容器 bind 的失败通过 result 返回，具体规则见下文。

表示能力使用示例：

```cpp
auto capability = source.type().capability<meta::representation_capability>();
if (!capability)
  return error::unsupported;
return capability.encode(source, consume_representation);
```

表示编码要求源实例与 capability属于同一类型及注册表，不匹配返回 invalid_argument。
解码不要求目标已存在；候选在 consume 回调返回后销毁，需要保留时显式复制或移动。

### capability 绑定实例

类型级 capability 不绑定对象。sequence_capability、associative_capability、optional_capability、
reference_capability、variant_capability 通过 bind(source) 返回对应的实例 view，保留 source
的编译期可写性。representation_capability 直接 encode/decode，不要求实例 view。

```cpp
auto sequence = source.type().capability<meta::sequence_capability>();
if (!sequence)
  return error::unsupported;
auto elements = sequence.bind(source); // result<对应只读或可写 sequence view>
if (!elements)
  return elements.error();
// 也可以一步查询并绑定：
auto same_elements = source.capability<meta::sequence_capability>();
```

能力缺失时查询返回空 capability。bind 对无效源返回 invalid_state，能力缺失或不可访问的
属性代理返回 unsupported，类型或注册表不匹配返回 invalid_argument。已有 as_sequence 等
实例便捷入口复用该绑定规则；可读借用不会自动升级为可写访问。

容器能力共享 container_capability 绑定实现，不增加注册表、能力 key 或独立状态。
视图继续遵循实例借用及容器结构修改后的失效规则，持有 capability 不保活被绑定的对象。

### 元素类型与枚举项

`type` 不再提供 element_type/enum_entries。sequence、associative、optional、reference
capability 提供 element_type()，分别返回序列元素、映射值、可选值、引用声明目标的类型。
查询前检查能力；variant 不提供该方法，它有多个分支，仍通过 variant_view::alternative(i) 查询。

```cpp
auto enumeration = type.capability<meta::enum_capability>();
if (enumeration) {
  auto choices = enumeration.entries();
  // choices 借用已注册枚举项；须保活 enumeration 或其注册表。
}
```

未注册原生枚举的能力有效，但 entries() 为空；非枚举返回空能力，其 entries() 也为空。
类型身份与注册状态仍通过 type 查询。编辑器下拉框及枚举名称编解码共用该条目来源。

### 能力头文件依赖

`type.hpp` 只提供通用类型查询入口，不包含具体 capability。使用方按需显式包含
`meta/capabilities/sequence_capability.hpp`、`enum_capability.hpp` 或
`representation_capability.hpp`；关联、可选、引用和 variant 能力定义在
`container_capability.hpp`。实例绑定还需包含 `instance.hpp`。仅声明友元不需要包含能力实现。
