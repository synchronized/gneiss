<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Type Registry 与反射元数据

## 当前范围

反射注册与查询使用 `src/engine/core/meta/registration.hpp` 和
`registry.hpp` 中的原生接口。Registry 的 C 函数、公共句柄表及 C 元数据缓存已删除。
旧 `reflection.h`、C 属性值布局和转换头已删除；Editor/Runtime 属性命令直接持有原生 `guid`。
IPC 使用 guid::string 的标准带连字符字符串编码 GUID，不传输原生对象布局。

当前支持类型、字段、显式属性访问器和显式单基类登记，内建 Transform 与 Camera 已接入。
类型构造和基类转换通过 object_ops 查询，序列化使用这些能力恢复对象；
不提供通用对象创建服务、注销、解冻、描述替换或动态模块卸载。

## 身份与注册

Meta 的类型与函数统一位于 `gneiss::engine::meta`，不保留旧命名空间别名。
`type_definition.properties` 保存注册描述；查询返回保活注册表的 `type` 和 `property_range`。
数学属性直接使用 `engine::math::vector3`、`engine::math::quaternion`，不提供属性专用别名。
属性编号与存档、IPC 中既有的 `"field_id"` 格式键保持不变。

文件按职责组织，不新增第二套注册表：

| 文件 | 职责与主要类型 |
| --- | --- |
| `meta/type_types.hpp` | 类型分类与枚举项 |
| `meta/property.hpp` | 属性 ID、标志、注册描述与访问器绑定 |
| `meta/type.hpp` | `type` 查询句柄 |
| `meta/type_definition.hpp` | 注册输入，组合对象能力与序列化绑定 |
| `meta/ops/object_ops.hpp` | 构造、基类指针转换和反射对象转换 |
| `meta/registry_fwd.hpp` | 注册表前置声明与 `registry_ref` 所有者 |
| `meta/registry.hpp` | 创建/注册/冻结、属性访问及能力查询 |
| `meta/registry.cpp` | 唯一注册表的私有存储与实现 |
| `meta/registration.hpp` | C++ Builder 注册入口与模板身份适配 |
| `meta/builtin_registration.cpp` | 内建 meta::object 多态根类型注册 |
| `serialization/diagnostic.hpp` | 编解码错误、路径与位置 |
| `serialization/binding.hpp` | 字段遍历、多态保存策略、自定义编解码与校验 |
| `serialization/json.hpp` | JSON 文本、字节与对象入口 |

内建组件身份位于 `engine/function/world/component_identity.hpp`，不属于通用 Meta。
`guid`、`type_id`、`value` 仍是 Core 基础类型，`type_identity` 仍是 traits 定制点。

`engine::guid` 保存 16 字节稳定类型身份，全零无效；`engine::type_id` 是进程内类型编号，
不用于反射存档。字段完整身份由稳定 GUID 与非零字段编号组成。名称用于 UTF-8 显示和诊断。
GUID 字节顺序与规范 UUID 文本去掉连字符后的顺序一致，不按主机端序解释。

`property_definition.type_guid` 表示字段值的语义类型，无需事先注册该类型。
`property_flags::read_only` 禁止为字段绑定 setter。

`create_registry()` 返回 `result<registry_ref>` 所有者；`register_type` 深拷贝名称和字段描述。
类型自身的稳定身份为 `type_definition.guid`、`type::guid()`；注册输入另有可选的运行期 `type_id`。
重复非空 GUID 或重复非空运行期 ID 的注册返回 `error::invalid_argument`，不覆盖已有记录。
普通 C++ 对象可仅注册运行期 type_id；GUID 与 type_id 不能同时为空。
多个无 GUID 类型通过 type_id 区分，nil GUID 查询始终返回 not_found。
注册名称不得为空或包含内嵌空字符，必须是合法 UTF-8；重复字段编号或名称也会被拒绝。

`bind_property` 绑定原生函数指针、类别和借用上下文；相同完整绑定可重复调用。
`freeze` 按 GUID 字节顺序和字段编号排序，之后禁止注册与绑定。重复冻结幂等成功。
冻结时同时建立包含继承属性的名称槽位索引，准备完成后统一提交；失败保留原注册内容，允许补齐后重试。
冻结后的已注册对象按 GUID 二分查询或运行期 ID 索引查询，属性按 ID/名称二分查询。
无 GUID 类型按进程类型 ID 排序，此顺序只用于当前进程枚举，不得持久化。
基础值、枚举及容器类型继续使用各自的既有解析路径，不承诺所有类型查找都已索引化。

无静态身份的外部类型可通过 `registration<T>{}.name("名称").guid(id)` 补充身份；
名称由注册器复制持有，再由 commit 深拷贝到注册表。与非空 traits 身份冲突时返回
invalid_argument。注册器只在显式 commit 时提交，重复 commit 返回 invalid_state。
字段仍使用 `.property<&T::member>(property_id, name)`，稳定编号不按字段顺序自动生成。

## 类型句柄查询

普通消费者通过 `find_type` / `type_of<T>` 获取保活注册表的 `meta::type`。
`name()` 借用注册对象名称；空句柄或没有注册名称的类型返回空字符串。
`find_property(property_id)` / `find_property(name)` 返回属性句柄，不复制整份属性列表。
空类型查询返回 `invalid_state`，非对象类型返回 `unsupported`，不存在的属性返回 `not_found`。
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
其他属性类型使用 builder 前须提供相应 GUID 特化，不能按编译器名称临时生成。

`registration.hpp` 是普通 C++ 类型注册的主要入口，通过 `registration<T>` 使用现有 Registry，
没有第二套运行期状态。动态描述仍可使用 `registry.hpp` 的非模板注册函数。
调用序列化模板须直接包含 `serialization.hpp`。不保留旧头文件转发层或 get/set 属性入口。
类型具有静态身份时可这样注册：

```cpp
return gneiss::engine::meta::registration<math::transform>{}
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
- commit 一次性登记类型及绑定，重复字段、身份冲突、冻结或分配失败不会留下该类型。
  builder 只允许提交一次；多个类型的注册不组成整体事务。
- 直接成员适用于无需额外约束的属性；带约束的 setter 必须先校验再修改。
  不支持的 value 类型在模板实例化时报错，不引入任意对象或偏移量访问。

底层 `register_type(registry, definition, bindings)` 的非空 bindings 必须与 properties 等长、顺序对应。
空 bindings 保留原有只登记描述、之后显式 bind_property 的入口。

## 查询与属性值

`type_count`、`type_at` 枚举冻结类型；`find_type`、`find_property` 返回句柄。
`find_type` 支持稳定 GUID 和运行期 `type_id` 两种入口，指向同一份记录。
`register_type<T>` 从 `type_identity<std::remove_cvref_t<T>>` 补齐名称、GUID 和运行期 ID；
显式名称或 GUID 与非空 traits 身份冲突时返回 `invalid_argument`。`type_of<T>` 解析规范化 C++ 类型，未注册对象返回 not_found，基础值无需重复登记。
模板去除顶层 cv/ref；注册 `void` 或传入不匹配的非空 `type_id` 返回 `invalid_argument`。
不对应 C++ 类型的描述保留空 `type_id`，仍可按 GUID 查询。运行期 ID 不进入文件或 IPC。
`property_definition` 的编号和标志分别使用 `property_id` 与 `property_flags`；`bind_property` 接收强类型字段编号。
查询以 result 返回值或错误；未冻结返回 `not_ready`，未知身份或越界索引返回 `not_found`，空引用返回
`invalid_handle`。类型查询输出拥有字段数组，名称借用 Registry，不能超过 Registry 生命周期。

原生属性值支持布尔、int64、uint64、float、double、UTF-8 字符串、稳定 GUID、三维向量和四元数。
浮点与数学值必须有限，字符串必须为无内嵌空字符的合法 UTF-8，GUID 值允许 nil；类型或对象身份是否允许 nil，由对应注册/业务规则校验。
字段专属范围和四元数归一化由 setter 校验，失败不得留下部分修改。

回调、read/write 及组件接口统一使用 [engine::value](value.md)。
getter 输出 `value&`，字符串复制进值；setter 接收 `const value&`，不保留输入引用。
不再存在借用值和拥有值之间的转换。`read_property` 返回 `result<value>`，成功快照拥有数据，失败不交付部分值；
`write_property` 直接传递同一份 const 值。

未绑定字段或缺少对应方向的访问器返回 `unsupported`。getter 抛异常或返回非法类别/值时返回
`internal`，`read_property` 不交付属性快照。setter 异常也转换为 `internal`；getter/setter 的分配异常转换为 `out_of_memory`。
基础 getter 接收 `const void* object`，setter 接收 `void* object`。对象仅在调用期间借用，Registry
不复制、不持有对象，也不解释 World/Entity 标识。调用方必须保证对象实际类型、寿命及线程访问合法。
空对象返回 `invalid_argument`；不支持运行期校验任意 void* 的实际类型。
setter 应先校验再修改，Core 不回滚第三方回调已经产生的副作用。Prefab 临时数据使用同一 setter，
不再提供独立的候选访问接口；跨字段联合校验由提交数据的调用方负责。

## 内建组件与生命周期

`meta::register_builtin_types` 注册基础值以及 `math::transform`、`math::camera_settings`，
支持默认表和显式独立表，不冻结注册表；重复注册返回 `invalid_argument`。
数学注册实现集中在 `core/meta/math_registration.cpp`，不依赖 World 或资源模块。
`camera_description` 等业务描述由资源内置注册负责，应用初始化依次注册 Meta 和资源类型。
稳定身份使用 `math::transform::type_guid`、`camera_description::type_guid`。

`function/scene/component_adapter.hpp` 提供 `read_component_property`、`write_component_property`：显式接收 `world&`、
Entity、类型 GUID 和字段 ID。先通过 World 接口校验线程与实体并定位节点；普通组件通过同一
适配器捕获、验证和安装，基础反射只操作副本。Transform 保留节点变换入口，确保层级传播。
Editor、Runtime 和运行时 Prefab 覆盖使用这些组件入口；Core 不依赖 World。

Transform 的反射对象为 `math::transform`；Camera 直接使用 `camera_description`，包含
`math::camera_settings settings` 和标记为编辑只读的 `bool is_primary`，不再注册独立属性包装类型。
相机参数通过 `settings` 的嵌套属性访问；活动状态由 World 捕获，序列化仍保存它。
组件运行时属性编辑入口拒绝写入标记为 `editor_read_only` 的字段；活动相机切换使用 World 专用接口。
Transform 字段为平移、旋转、缩放；Camera 的 `settings` 内含 `fov`、`near`、`far`。

Registry 使用原生共享引用管理，不经全局句柄表。独立表在最后一个引用释放时销毁；
默认表由引擎模块持有至进程退出，不随应用关闭清空。
场景服务和场景实例持有引用，Prefab 操作在调用期间借用。
元数据与回调上下文的使用必须在相应所有者销毁前结束；Registry 不拥有回调上下文。

初始化阶段由调用方串行注册和绑定；冻结后的查询与访问器查找可以并发调用。
目标对象的线程规则由访问器决定，内建 World 操作仅允许所属线程调用。

## 类型编解码

`serialization/binding.hpp` 提供可选 `serialization::binding`，
`serialization/diagnostic.hpp` 提供拥有错误位置文本的 `serialization::diagnostic`。能力与类型、字段一起登记到同一 Registry，不依赖资源层。
`registration<T>::serialization<Encode, Decode>()` 推导类型适配；任一函数可为 nullptr，
但至少具备一个方向。同一 builder 不允许重复设置。
每个类型当前只登记一组编解码函数，自定义文件格式标识及版本由 codec 自行校验，不提供任意 capability 或通用对象工厂。

`find_serialization` 按运行期 type_id 查询冻结 Registry 中的函数表，不登记重复的格式名称。
`serialization::json::serialize_bytes<T>` 返回 `result<vector<byte>, serialization::diagnostic>`，`deserialize<T>` 返回
`result<T, serialization::diagnostic>`。成功结果拥有值，失败结果仅携带错误码、路径、字节偏移和消息。
`serialization::diagnostic.error` 保存错误码；bad_alloc 转为 out_of_memory，其余异常转为 internal。
Builder 接收返回上述结果的 C++ codec；擦除后的解码回调返回 `result<ref<void>, serialization::diagnostic>`，
模板接口按已登记的运行期类型 ID 找到回调后移出具体对象，不要求默认构造或移动赋值。
未冻结返回 not_ready，未知类型返回 not_found，缺少方向返回 unsupported，空 Registry 返回 invalid_handle。
回调在 Registry 锁外执行，要求可并发处理不同对象；对象与输入只在调用期间借用。
输入大小限制由文件读取入口控制，反射不负责 I/O、缓存、任务或 GPU 上传。

资源层 `register_builtin_resource_types()` 统一登记材质、采样参数与枚举、ActionMap、Scene、Prefab
的类型及 codec，统一使用默认表。注册集中在 `resource/builtin_resource_registration.cpp`；
codec 文件只提供编解码入口。首次失败立即返回，不回滚已注册内容；重复注册失败。
组件描述随资源入口一起登记。冻结前依次登记 Meta 内置类型、World 基础值和资源类型；默认 initialize_meta 已按依赖顺序集中调用，
无需资源消费者重复注册。Meta 的 register_builtin_types 提供默认表和显式表版本，独立注册表可据此读写 engine::value，无需加载资源模块。
材质直接注册 parameters、texture_uris 及嵌套字段，格式包装器只处理格式头；实际格式见[材质格式](render-asset-formats.md#material)。

## 反射 JSON 与对象校验

`registration<T>.serialization()` 显式启用共享字段遍历，要求 T 可默认构造。
已登记的自定义 encode/decode 按方向优先；只有缺少该方向函数时才回退到反射 JSON。
同一 Builder 可各调用一次自定义和反射注册，两者顺序不限；未启用反射的类型保持原行为。

```cpp
// math::camera_settings 已独立注册 fov / near / far 和参数校验。
meta::registration<camera_description>{}
    .property<&camera_description::settings>(
        meta::property_id{1}, "settings", meta::property_flags::required)
    .property<&camera_description::is_primary>(
        meta::property_id{4}, "is_primary", meta::property_flags::required)
    .serialization()
    .commit();
```

属性名直接作为 JSON 字段名，支持 bool、int64、uint64、float、double、string、guid、
vector2、vector3、vector4、quaternion；数学值按分量数组保存，GUID 使用规范小写带连字符文本。
属性的稳定身份由 `type_identity` 提供。支持独立注册的嵌套值对象及[容器视图](reflection-containers.md)列出的容器；不支持指针图。
沿用已有属性访问边界：整数严格校验范围，浮点必须有限，字符串为无 NUL 的合法 UTF-8，
GUID 值不能为 nil。四元数的归一化等业务约束由类型校验器决定。

缺失普通字段保留 T 的默认值；`property_flags::required` 字段缺失返回 invalid_argument。
null 不等于缺失。只读属性不输出，也不从输入写入；read_only 与 required 不能组合。
未知字段忽略，重复 JSON 键拒绝；普通类型不会自动保留未知字段。
字段错误路径使用 JSON Pointer 转义，语法错误提供字节偏移。
沿用属性访问语义：getter 返回非法值为 internal，输入值非法为 invalid_argument。

`.validate<Fn>()` 接受返回 error 或 serialization::diagnostic 的只读校验函数。
成功诊断的 error 为 success。编码前、解码填充结束后各校验一次，适用于自定义 codec 与反射 JSON。
解码在独立临时对象中写属性，失败析构临时对象；访问器不得产生需要回滚的外部副作用。
校验器和 codec 异常在分派边界转换为 out_of_memory 或 internal。

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

## 元数据与属性值查询

`find_type`、`type_of<T>`、`type_at` 返回 `result<type>`；
`find_property` 返回 `result<property>`，`find_serialization` 返回 `result<serialization::binding>`。
句柄保活冻结 Registry；名称借用该表，不保活业务对象。
`read_property` 返回 `result<value>`；成功快照拥有字符串等数据，可在 Registry 释放后使用。
getter 失败、异常或返回不匹配／无效值时仅返回错误，不交付部分属性快照。
写属性仍返回 error；编解码与序列化模板返回 result。

## 无 GUID 对象边界

`registration<T>{}.name("名称")` 可以注册没有持久 GUID 的 C++ 对象；名称已有 traits 时可省略。
`type_of<T>`、view、member、getter/setter、嵌套对象按运行期 type_id 访问，
不能通过 nil GUID 查找这些类型。普通静态类型 JSON 内容不携带身份，可正常编解码。
多态 `$type_guid/$content` 必须有非空 GUID，否则编码返回 unsupported。
对象 value 快照使用稳定 GUID 标识结构，capture/apply 对无 GUID 对象返回 unsupported；
不把进程 type_id 写入文件，也不把所有 nil GUID 对象视为同一种快照。

## 默认注册表与启动

`meta::default_registry()` 返回进程共享的 `result<registry_ref>`，只创建表，不登记内置类型、不冻结。
定义位于 engine 编译单元；各模块应链接同一引擎，不能把引擎静态复制到多个动态库后期待共享状态。
未冻结时查询返回 `not_ready`。`meta::registration<T>{}`、`register_type<T>(definition)`、
`type_of<T>()`、属性访问和 `serialization::json::serialize(object)` / `serialization::json::deserialize<T>(bytes)` 使用默认表；
显式 registry 重载保持原行为。

业务模块不得直接调用 `meta::default_registry()`；默认表获取集中在 Meta 内部，
普通调用使用无 registry 的查询、注册、序列化及视图入口。
`const_instance::from_object(type_id, address)` 和可写版本使用默认表，错误按 result 返回。
`serialize_instance(view)`、`deserialize_instance(view, bytes)` 及内部 DOM 适配使用
视图自身绑定的注册表，不重新获取默认表，因此独立表的字段与 codec 语义保持不变。
Meta 内部递归与 codec 回调仍保留显式上下文；注册表机制测试允许直接验证默认表。

`engine/function/meta_registration.hpp` 的 `engine::initialize_meta(callback = nullptr)` 在启动阶段
串行登记内置 World 属性、资源 codec、项目类型，再冻结。callback 不带参数，
可直接使用默认 builder。需要自定义类型的宿主在首次 `application::create` 前调用带 callback 的入口；
application 自动调用无参入口，重复无参调用返回首次结果。独立工具应显式完成初始化。
初始化期间不得并行修改或手动冻结默认表，callback 不得重入初始化函数。

首次注册失败或异常会保留失败结果，表不冻结；后续无参调用返回原错误，应修复后重新启动进程。
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
`camera_settings` 的身份通过外部 traits 定义，参数注册和校验与相机组件一起安装，数学类型本身不依赖 Meta。
Camera 直接绑定 settings 和 is_primary，文件内容为：

```json
{"settings":{"fov":1.0,"near":0.1,"far":100.0},"is_primary":false}
```

未发布的原扁平相机描述不再读取；仓库资产与生成脚本同步更新。
World 运行期属性与 IPC 仍保留既有名称，这些协议不等于资产文件中的嵌套结构。

## 属性句柄与范围

`type::properties()` 与只读实例的 `properties()` 返回 `result<property_range>`。
范围和迭代器保活冻结注册表，直接访问冻结属性数组，不为每次遍历复制属性列表。
范围只读；下标访问要求 index 小于 size，end 不能解引用。

```cpp
auto fields = root->properties();
if (!fields) return fields.error();
for (auto property : *fields) {
  auto member = property.get(*root);
  if (!member) return member.error();
  // property.name()/type()/flags()/capabilities() 查询同一份冻结定义。
}
```

`property::get` 校验注册表及字段声明类型；即使属性 ID 相同，也不能用于不相关类型。
派生对象上的继承属性继续通过实例访问完成基类地址调整。
句柄不持有目标对象；只读访问、getter 临时值与可写访问规则同下述实例视图。
实例现命名为只读 const_instance、可写 instance；旧 mutable_instance 名称与静态 from 模板工厂已移除。
元数据查询统一返回 type/property 句柄，不再提供复制信息结构的查询接口。

## 实例视图

`meta/instance.hpp` 提供只读 `const_instance` 和可写 `instance`，不新增对象所有者或注册表。
根视图通过 `meta::view(registry, object)` 创建，也可省略 registry 使用默认表；
可写对象返回 result<instance>，const 对象返回 result<const_instance>，需要主动只读时传 std::as_const(object)；仅接受实际对象的左值，
不接受临时对象。根类型必须已登记，注册表必须冻结。

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
基础属性支持 `read_value()`；`kind()` 区分 invalid、empty、scalar、object、sequence、associative、optional、reference、variant；
`scalar_kind()` 区分基础值类别。`guid()` 返回静态稳定身份，容器通常为空；
`type_id()` 为对象、容器和容器元素提供运行期身份，普通标量属性仍通过 value_kind/GUID 标识。

基础属性视图保存父对象和属性位置，读取时调用原 getter，返回自有 value。
即使 getter 返回临时字符串，也不会借用该临时值；已经读取的快照不随父对象变化。
再次 read_value 则读取父对象当前值。基础属性的 object_data 返回 unsupported。
对象视图的 object_data 返回借用的对象地址，不复制嵌套对象。

`meta::view(T&)` 返回可写视图；`as_const()` 转为只读视图，不提供反向转换。
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

`serialization(true)` 启用严格字段检查；默认 false 保留忽略未知字段行为，两者都拒绝重复键。
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

`meta::const_instance::type()`（包括可写实例）返回轻量 `meta::type`。视图持有原注册表，
不拥有业务对象；`kind()` 查询结构类别，`scalar_kind()` 查询 value 的承载类别，
`is_enum()` 区分 C++ 枚举与整数，`id()` 保留枚举自己的运行期类型身份。
枚举尚无独立稳定 GUID 注册入口，因此类型视图的枚举 `guid()` 为空，不冒用整数 GUID。

`enum_entries()` 从冻结注册表借用同一份枚举项；未冻结返回 not_ready，未注册返回
not_found，非枚举返回 unsupported，默认无效视图返回 invalid_state。借用结果不得超过
类型视图或注册表的生命周期。`properties()` 返回保活注册表的属性范围；`element_type()` 返回
序列元素类型或引用静态目标类型，关联容器返回映射值类型。

属性面板的 `draw_property_value` 接收类型视图和 value，检查存储类别匹配后从类型查询
枚举选项。枚举容器元素也走此路径；注册表无需为每个属性复制枚举列表。
property 句柄通过 type() 返回属性值类型，不重复保存
kind/category/object_type/enum_type/choices/type_guid。属性提供 id、name、flags 和 capabilities 查询。
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
`element_type()` 返回静态目标类型；目标对象类型仍需注册。ref<void> 不支持字段反射。

通过 `const_instance::as_reference()` 获取 `reference_view`，再用 `get()` 获取只读借用目标。
`instance::as_reference()` 返回可写引用视图，get 仅对 ref<T> 的可写目标有效；
ref<const T> 返回 unsupported，可通过引用视图的 as_const().get() 读取。
has_value 检查是否非空；视图本身不持有目标强引用。
reference_view::get 对空引用返回 not_found。引用不是标量；可变 meta::object 目标支持 value 强引用传递，其他引用的 read_value 返回 unsupported。

`mutable_reference_view::assign(source)` 接受只读引用视图，对完全相同的 ref 类型共享控制块，不复制目标；
不同目标类型（包括 const 差异）返回 invalid_argument，不隐式推断派生转换。
引用视图的 `reset()` 释放当前引用，不影响其他强引用；视图不会自动延长目标寿命，
重置/替换后应重新获取目标视图。
通过 `reference_view::owner()` 显式保活目标；`actual_type()` 查询实际运行类型。
可写视图的 `assign_owner(id, owner)` 仅接受声明目标类型的 ID，调用方须保证擦除指针与 ID 匹配。
修改目标会被所有共享拥有者观察到，不提供写时复制。
实例的 `registry()` 返回所持有的注册表；受 setter 约束的复合属性通过
`requires_replacement()` 判断整体替换要求，`replace(initialize)` 在独立候选上初始化，
成功后才调用 setter。普通成员继续使用 `assign`，序列化不访问 Meta 私有状态。

默认反射 JSON 按值内联引用：空引用输出 null，普通 ref<T> 输出目标字段。
声明类型通过 registration<T>::polymorphic() 显式启用多态后，输出
`{"$type_guid":"实际类型 GUID","$content":{...}}`。具体类型通过 base<Base>() 注册
安全基类转换，未知/无关类型在构造前拒绝；未声明多态的引用不能保存派生对象。
具体类型可使用反射字段或已有自定义 JSON codec，后者仍需输出 JSON 对象。

多态对象继承 meta::object，具体类可用 object_type<Derived, Base> 提供类型身份与完整对象地址。
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

属性标记 `property_flags::enum_name` 使 JSON 使用枚举注册项的名称。C++ 属性保持枚举类型，
类型视图仍可查询枚举列表，`read_value()` 仍返回底层 int64/uint64。未标记属性保持数字编码。
名称编码拒绝未注册值；解码只接受已注册名称，拒绝数字、null 和未知名称，诊断定位到属性路径。
该标记用于枚举属性，不是任意容器或对象的格式转换选项。

Texture 描述注册 source、color_space、usage 三个必填字段；后两者使用 enum_name。
文件保持 gneiss.texture v2、字符串枚举及 normal 必须配 linear 的校验。调用描述 codec 前
需初始化默认 Meta 注册表；不会自动启动 Application 或 GPU。

### 非标量 getter/setter 属性

`registration<T>::property<Getter, Setter>(id, name)` 支持嵌套对象、序列、关联容器和引用。
非标量 getter 必须返回稳定的 const U&，不接受临时值；setter 接收完整 const U&，返回 error
或 void，负责先校验再修改，失败须保持旧属性。未知 U 仍需按原规则注册。

此绑定的 instance 支持 assign/assign_object 整项替换，但不提供可写对象地址、
可写容器/引用视图或可写子属性；通过 as_const() 读取嵌套内容。属性替换后重新获取借用视图。
反序列化先创建独立默认候选并完成解码，然后调用 setter；解析失败不调用 setter。
setter 返回的业务错误定位到属性，嵌套解码错误保留具体子路径。
这只保证单个受约束属性的替换边界，不承诺整个父对象或任意回调副作用的自动回滚。


## 可选值反射

`std::optional<T>` 对应独立 `type_kind::optional` 和 `optional_ops`，不改变值所有权，
也不引入共享引用。`type::element_type()` 返回 T 的类型。
通过 `const_instance::as_optional()` 获取只读借用视图，提供 `has_value()`、`get()`；
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
`serialization::json::serialize` 返回 JSON 文本，`serialize_bytes` 返回相同 JSON 的字节；
`deserialize<T>` 接受文本或字节。两种入口均提供 context-first 和默认上下文版本。
Meta 不再提供含糊的 serialize/deserialize 入口。

注册表只保存通用 encode/decode、构造、继承、校验和字段遍历策略。
`serialization()` 启用字段遍历，`serialization<Encode, Decode>()` 提供特殊类型的通用操作。
资源二进制协议通过 asset_decoder 显式注入解码函数。

字段遍历已独立于 JSON 后端，接口与限制见 [序列化](serialization.md)。

## 类型能力与序列化绑定

`type_definition.object` 保存 `meta::object_ops`，通过 `find_object_ops` 查询。
注册表冻结前返回 not_ready，未知类型返回 not_found，空注册表返回 invalid_handle。
已登记但没有相应能力的类型返回空函数项；调用前须检查函数项。
`type_definition.serialization` 保存 `serialization::binding`，仍通过 find_serialization 查询。
两者由同一注册表保存；继承关系不自动启用多态保存策略，校验时机保持不变。
