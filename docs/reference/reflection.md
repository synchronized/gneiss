<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Type Registry 与反射元数据

## 当前范围

反射注册与查询使用 `src/engine/core/meta/registration.hpp` 和
`registry.hpp` 中的原生接口。Registry 的 C 函数、公共句柄表及 C 元数据缓存已删除。
旧 `reflection.h`、C 属性值布局和转换头已删除；Editor/Runtime 属性命令直接持有原生 `guid`。
IPC 仍使用既有 32 位十六进制字符串编码 GUID，不传输原生对象布局。

当前支持类型、字段和显式属性访问器，内建 Transform 与 Camera 已接入；仅在反射 JSON 解码内部默认构造临时对象，不提供通用对象创建服务、
继承、注销、解冻、描述替换或动态模块卸载。

## 身份与注册

Meta 的类型与函数统一位于 `gneiss::engine::meta`，不保留旧命名空间别名。
`type_definition.properties` 和 `type_info.properties` 分别保存注册描述和查询结果。
数学属性直接使用 `engine::math::vector3`、`engine::math::quaternion`，不提供属性专用别名。
属性编号与存档、IPC 中既有的 `"field_id"` 格式键保持不变。

文件按职责组织，不新增第二套注册表：

| 文件 | 职责与主要类型 |
| --- | --- |
| `meta/property.hpp` | 属性 ID、标志、注册描述、查询结果及访问器绑定 |
| `meta/type.hpp` | `type_definition`、`type_info`，不依赖注册表 |
| `meta/serialization_types.hpp` | 编解码函数表和诊断值，不依赖注册表 |
| `meta/registry.hpp` | `registry_ref`、创建/注册/冻结、查询、属性访问和编解码函数表查询 |
| `meta/registry.cpp` | 唯一注册表的私有存储与实现 |
| `meta/registration.hpp` | C++ Builder 注册入口与模板身份适配 |
| `meta/serialization.hpp` | `serialize<T>`、`deserialize<T>` 调用模板 |

内建组件身份位于 `engine/function/world/component_identity.hpp`，不属于通用 Meta。
`guid`、`type_id`、`value` 仍是 Core 基础类型，`type_identity` 仍是 traits 定制点。

`engine::guid` 保存 16 字节稳定类型身份，全零无效；`engine::type_id` 是进程内类型编号，
不用于反射存档。字段完整身份由稳定 GUID 与非零字段编号组成。名称用于 UTF-8 显示和诊断。
GUID 字节顺序与规范 UUID 文本去掉连字符后的顺序一致，不按主机端序解释。

`property_definition.type_guid` 表示字段值的语义类型，无需事先注册该类型。
`property_flags::read_only` 禁止为字段绑定 setter。

`create_registry()` 返回 `result<registry_ref>` 所有者；`register_type` 深拷贝名称和字段描述。
类型自身的稳定身份为 `type_definition.guid`、`type_info.guid`；注册输入另有可选的运行期 `type_id`。
重复 GUID 或重复非空运行期 ID 的注册返回 `error::invalid_argument`，不覆盖已有记录。
注册名称不得为空或包含内嵌空字符，必须是合法 UTF-8；重复字段编号或名称也会被拒绝。

`bind_property` 绑定原生函数指针、类别和借用上下文；相同完整绑定可重复调用。
`freeze` 按 GUID 字节顺序和字段编号排序，之后禁止注册与绑定。重复冻结幂等成功。

## C++ 类型注册

`type_identity<T>` 优先读取类内静态 `type_name` 和 `type_guid`，也允许外部特化；
未声明名称时回退到 `type_name<T>`，未声明 GUID 时为空。cv/ref 委托规范化类型。
当前 bool、float、vector3、quaternion 的 traits 提供内建字段原有的稳定 GUID；
其他属性类型使用 builder 前须提供相应 GUID 特化，不能按编译器名称临时生成。

`registration.hpp` 是普通 C++ 类型注册的主要入口，通过 `type_builder<T>` 使用现有 Registry，
没有第二套运行期状态。动态描述仍可使用 `registry.hpp` 的非模板注册函数。
调用序列化模板须直接包含 `serialization.hpp`。不保留旧头文件转发层或 get/set 属性入口。
类型具有静态身份时可这样注册：

```cpp
return gneiss::engine::meta::type_builder<camera_properties>{}
    .schema_version(1)
    .readonly<&camera_properties::is_primary>(camera_is_primary_field, "is_primary")
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

`type_count`、`describe_type_at` 枚举冻结元数据；`describe_type`、`describe_property` 直接查询记录。
`describe_type` 支持稳定 GUID 和运行期 `type_id` 两种入口，指向同一份记录。
`register_type<T>` 从 `type_identity<std::remove_cvref_t<T>>` 补齐名称、GUID 和运行期 ID；
显式名称或 GUID 与非空 traits 身份冲突时返回 `invalid_argument`。`describe_type<T>` 使用 `runtime_type_of<T>()` 查询。
模板去除顶层 cv/ref；注册 `void` 或传入不匹配的非空 `type_id` 返回 `invalid_argument`。
不对应 C++ 类型的描述保留空 `type_id`，仍可按 GUID 查询。运行期 ID 不进入文件或 IPC。
`property_definition` 的编号和标志分别使用 `property_id` 与 `property_flags`；`bind_property` 接收强类型字段编号。
查询以 result 返回值或错误；未冻结返回 `not_ready`，未知身份或越界索引返回 `not_found`，空引用返回
`invalid_handle`。类型查询输出拥有字段数组，名称借用 Registry，不能超过 Registry 生命周期。

原生属性值支持布尔、int64、uint64、float、double、UTF-8 字符串、稳定 GUID、三维向量和四元数。
浮点与数学值必须有限，字符串必须为无内嵌空字符的合法 UTF-8，GUID 不得全零。
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

`register_reflection` 为未冻结 Registry 注册 Transform 和 Camera；重复调用返回 `invalid_argument`。
World 属性注册和读写统一使用默认表，不再提供独立组件注册表工厂。
稳定身份使用 `transform_component_type`、`camera_component_type` 或对应原生查询函数。

`function/scene/component_adapter.hpp` 提供 `read_component_property`、`write_component_property`：显式接收 `world&`、
Entity、类型 GUID 和字段 ID。先通过 World 接口校验线程与实体并定位节点；普通组件通过同一
适配器捕获、验证和安装，基础反射只操作副本。Transform 保留节点变换入口，确保层级传播。
Editor、Runtime 和运行时 Prefab 覆盖使用这些组件入口；Core 不依赖 World。

Transform 的反射对象为 `transform`；Camera 为 World 层的 `camera_properties`，包含
`camera_settings settings` 和只读 `bool is_primary`。活动状态由 World 读出，不在基础 getter 中查询。
Camera GUID 和字段编号不变，C++ 模板查询使用 `describe_type<camera_properties>`。
Transform 字段为平移、旋转、缩放；Camera 字段为视场角、近裁剪面、远裁剪面与只读活动状态。

Registry 使用原生共享引用管理，不经全局句柄表。独立表在最后一个引用释放时销毁；
默认表由引擎模块持有至进程退出，不随应用关闭清空。
场景服务和场景实例持有引用，Prefab 操作在调用期间借用。
元数据与回调上下文的使用必须在相应所有者销毁前结束；Registry 不拥有回调上下文。

初始化阶段由调用方串行注册和绑定；冻结后的查询与访问器查找可以并发调用。
目标对象的线程规则由访问器决定，内建 World 操作仅允许所属线程调用。

## 类型编解码

`serialization.hpp` 提供可选 `serialization_binding` 和拥有错误位置文本的
`serialization_diagnostic`。能力与类型、字段一起登记到同一 Registry，不依赖资源层。
`type_builder<T>::serialization<Encode, Decode>()` 推导类型适配；任一函数可为 nullptr，
但至少具备一个方向。同一 builder 不允许重复设置。
每个类型当前只登记一组编解码函数，自定义文件格式标识及版本由 codec 自行校验，不提供任意 capability 或通用对象工厂。

`find_serialization` 按运行期 type_id 查询冻结 Registry 中的函数表，不登记重复的格式名称。
`serialize<T>` 返回 `result<vector<byte>, serialization_diagnostic>`，`deserialize<T>` 返回
`result<T, serialization_diagnostic>`。成功结果拥有值，失败结果仅携带错误码、路径、字节偏移和消息。
`serialization_diagnostic.error` 保存错误码；bad_alloc 转为 out_of_memory，其余异常转为 internal。
Builder 接收返回上述结果的 C++ codec；擦除后的解码回调返回 `result<ref<void>, serialization_diagnostic>`，
模板接口按已登记的运行期类型 ID 找到回调后移出具体对象，不要求默认构造或移动赋值。
未冻结返回 not_ready，未知类型返回 not_found，缺少方向返回 unsupported，空 Registry 返回 invalid_handle。
回调在 Registry 锁外执行，要求可并发处理不同对象；对象与输入只在调用期间借用。
输入大小限制由文件读取入口控制，反射不负责 I/O、缓存、任务或 GPU 上传。

资源层 `register_builtin_resource_types()` 统一登记材质、采样参数与枚举、ActionMap、Scene、Prefab
的类型及 codec，统一使用默认表。注册集中在 `resource/builtin_resource_registration.cpp`；
codec 文件只提供编解码入口。首次失败立即返回，不回滚已注册内容；重复注册失败。
组件描述随资源入口一起登记。冻结前先登记 World 基础值；默认 initialize_meta 已按依赖顺序集中调用，
无需资源消费者重复注册。
材质直接注册 parameters、texture_uris 及嵌套字段，格式包装器只处理格式头；实际格式见[材质格式](render-asset-formats.md#material)。

## 反射 JSON 与对象校验

`type_builder<T>.serialization()` 显式启用通用 JSON 对象读写，要求 T 可默认构造。
已登记的自定义 encode/decode 按方向优先；只有缺少该方向函数时才回退到反射 JSON。
同一 Builder 可各调用一次自定义和反射注册，两者顺序不限；未启用反射的类型保持原行为。

```cpp
// math::camera_settings 已独立注册 fov / near / far 和参数校验。
meta::type_builder<camera_description>{}
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

`.validate<Fn>()` 接受返回 error 或 serialization_diagnostic 的只读校验函数。
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

`describe_type`（GUID、type_id、模板）、`describe_type_at` 返回 `result<type_info>`；
`describe_property` 返回 `result<property_info>`，`find_serialization` 返回 `result<serialization_binding>`。
描述内的名称仍借用冻结 Registry，其有效期不超过 Registry 的生命周期。
`read_property` 返回 `result<value>`；成功快照拥有字符串等数据，可在 Registry 释放后使用。
getter 失败、异常或返回不匹配／无效值时仅返回错误，不交付部分属性快照。
写属性仍返回 error；编解码与序列化模板返回 result。

## 默认注册表与启动

`meta::default_registry()` 返回进程共享的 `result<registry_ref>`，只创建表，不登记内置类型、不冻结。
定义位于 engine 编译单元；各模块应链接同一引擎，不能把引擎静态复制到多个动态库后期待共享状态。
未冻结时查询返回 `not_ready`。`meta::type_builder<T>{}`、`register_type<T>(definition)`、
`describe_type<T>()`、属性访问和 `serialize(object)` / `deserialize<T>(bytes)` 使用默认表；
显式 registry 重载保持原行为。

`engine/function/meta_registration.hpp` 的 `engine::initialize_meta(callback = nullptr)` 在启动阶段
串行登记内置 World 属性、资源 codec、项目类型，再冻结。callback 不带参数，
可直接使用默认 builder。需要自定义类型的宿主在首次 `application::create` 前调用带 callback 的入口；
application 自动调用无参入口，重复无参调用返回首次结果。独立工具应显式完成初始化。
初始化期间不得并行修改或手动冻结默认表，callback 不得重入初始化函数。

首次注册失败或异常会保留失败结果，表不冻结；后续无参调用返回原错误，应修复后重新启动进程。
已启动后再提供 callback 返回 `invalid_state`。不支持运行期扩充、重置默认表或插件热卸载。
独立 registry 仍可通过 `create_registry()` 创建并按原接口注册、冻结，不受默认表状态影响。

`type_info::type_id` 提供已注册 C++ 类型的进程内身份，供 GUID 分派后进行类型检查；不会持久化。
默认启动也注册 Camera、MeshRenderer 的描述类型与 codec，文件协议见[场景格式](scene-format.md)。

## 嵌套值对象

成员绑定 `.property<&T::member>()` 可指向独立注册的 C++ 值对象。
`property_info.type.id()` 为成员的运行期类型 ID，`type.guid()` 为其稳定身份，
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

## 实例视图

`meta/instance.hpp` 提供只读 `instance` 和可写 `mutable_instance`，不新增对象所有者或注册表。
根视图通过 `from(registry, object)` 创建，也可省略 registry 使用默认表；仅接受实际对象的左值，
不接受临时对象。根类型必须已登记，注册表必须冻结。

```cpp
auto root = meta::instance::from(registry, camera);
if (!root) return root.error();
auto settings = root->property("settings");
if (!settings) return settings.error();
auto near = settings->property("near");
if (!near) return near.error();
auto snapshot = near->read_value(); // result<value>，拥有读取结果
```

`property(name)` 和 `property(property_id)` 返回同一种视图。对象支持 `properties()` 枚举，
基础属性支持 `read_value()`；`kind()` 区分 invalid、scalar、object、sequence、associative、optional；
`scalar_kind()` 区分基础值类别。`guid()` 返回静态稳定身份，容器通常为空；
`type_id()` 为对象、容器和容器元素提供运行期身份，普通标量属性仍通过 value_kind/GUID 标识。

基础属性视图保存父对象和属性位置，读取时调用原 getter，返回自有 value。
即使 getter 返回临时字符串，也不会借用该临时值；已经读取的快照不随父对象变化。
再次 read_value 则读取父对象当前值。基础属性的 object_data 返回 unsupported。
对象视图的 object_data 返回借用的对象地址，不复制嵌套对象。

`mutable_instance::from` 只接受可写左值；`as_const()` 转为只读视图，不提供反向转换。
可写 property 拒绝只读/无 setter 的属性，通过 write_value 执行原 setter 的校验与错误语义。
只读属性仍可通过 as_const().property 读取。单字段编辑不自动执行整对象跨字段校验，业务方应在
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
source_schema_version、dependencies 等运行状态不注册为持久化字段。
Scene/Prefab 的组件动态分派和 Prefab 覆盖使用专门 codec；保存直接从描述生成文件，
不合并旧 JSON 或保留未知扩展字段。Scene 的 parent 键仍映射原生 parent_uuid，不因本次内部迁移改变现有场景格式。

`serialization(true)` 启用严格字段检查；默认 false 保留忽略未知字段行为，两者都拒绝重复键。
自定义 codec 可接收首参数 `const registry_ref&`，收到当前调用表；不接收该参数的简单函数仍可注册。
同时启用反射 JSON 和自定义 codec 后，格式包装器可调用 serialize_reflected/deserialize_reflected
跳过本类型自定义函数、直接读写注册字段；嵌套 codec 和业务校验仍生效，不是绕过校验的入口。
材质文件包装仅处理 format/version，直接读写 v6 原生描述，不再创建文件数据副本类型。
材质、Scene/Prefab 及其嵌套组件使用已初始化并冻结的默认表；业务注册入口不接受独立表。

原生窄整数和枚举通过现有 int64/uint64 标量访问，写入先检查原生底层范围；
枚举定义域由所属类型 validate 校验。它们的属性 GUID 表示规范化后的整数存储类型。
vector4 使用独立 vec4 标量身份和四分量 JSON 数组；旧 IPC 的四元数类型不承载 vector4。

### 资源文件外层包装

Material 与 Action Map 使用 resource/detail/reflected_asset_codec.hpp 共用格式包装：
验证唯一的 format/version、对象根与配置的字节上限，内容交给反射字段序列化。
编码拒绝反射字段占用 format/version 名称，并在添加格式头后检查完整输出大小。
Material 保持版本 6，非零未知版本返回 unsupported；Action Map 保持版本 1 与 1 MiB 上限，
版本不符返回 invalid_argument。业务字段校验仍由各类型的 validate 完成。

vector2 使用独立 vec2 身份及 `[x,y]` 数组，严格检查两个有限 float 分量。
已有 value_kind 编号不变，vec2 追加于末尾。Prefab 覆盖格式支持 vec2/vec4 的独立 kind，
不把四分量向量解释成四元数。旧 Runtime IPC 尚无 vec2/vec4 表示，明确返回 unsupported。

## 类型视图

`meta::instance::type()`（包括可写实例）返回轻量 `meta::type`。视图持有原注册表，
不拥有业务对象；`kind()` 查询结构类别，`scalar_kind()` 查询 value 的承载类别，
`is_enum()` 区分 C++ 枚举与整数，`id()` 保留枚举自己的运行期类型身份。
枚举尚无独立稳定 GUID 注册入口，因此类型视图的枚举 `guid()` 为空，不冒用整数 GUID。

`enum_entries()` 从冻结注册表借用同一份枚举项；未冻结返回 not_ready，未注册返回
not_found，非枚举返回 unsupported，默认无效视图返回 invalid_state。借用结果不得超过
类型视图或注册表的生命周期。`properties()` 返回对象属性快照；`element_type()` 返回
序列/optional 元素类型，关联容器返回映射值类型。

属性面板的 `draw_property_value` 接收类型视图和 value，检查存储类别匹配后从类型查询
枚举选项。枚举容器元素也走此路径；注册表无需为每个属性复制枚举列表。
保留 type_info 快照；property_info 的声明类型统一由 `type` 字段提供，不再重复保存
kind/category/object_type/enum_type/choices/type_guid。属性保留 id、name、flags 和 capabilities。
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

面板和 JSON 序列化通过类型视图判断结构和标量类别；具体读写仍由 instance 完成。
包含自定义 getter/setter 的属性不会因新增类型操作表而绕过访问器。

## 对象所有权

`engine/core/ref.hpp` 提供 `ref<T>` 与 `make_ref<T>`，不要求类型继承共同基类。
资源层 `asset_ref<T>` 是它的语义别名。反射解码对象、组件描述的不可变内容以及编辑器
整对象编辑载荷共用该拥有者；复制保留同一控制块，转换为 const/void 不改变删除器。

`make_ref` 创建普通 CPU 对象；需要线程限定销毁的资源继续使用资源工厂的自定义删除器。
`get()` 和 `meta::instance` 是借用入口，调用方须保留拥有者，不能由借用指针重新创建所有权。
共享引用不保证对象数据线程安全，引用环须由弱引用或对象 ID 打破。

引用属性的持久化支持范围见下节；不代表任意共享对象图均可自动编解码。
资源标识、场景 GUID 和内嵌组件描述仍按各自已有格式处理。设计边界见
[ADR-098](../decisions/ADR-098-object-reference-ownership.md)。

### 引用属性访问

注册 `ref<T>` 成员后，`property.type.kind()` 为 `type_kind::reference`，
`element_type()` 返回静态目标类型；目标对象类型仍需注册。ref<void> 不支持字段反射。

通过 `instance::dereference()` 获取只读借用目标；`mutable_instance::dereference()`
仅对 ref<T> 的可写目标有效，ref<const T> 返回 unsupported，可通过 as_const 读取。
空引用返回 not_found；引用不是标量，read_value 返回 unsupported。

`mutable_instance::assign(source)` 对完全相同的 ref 类型共享控制块，不复制目标；
不同目标类型（包括 const 差异）返回 invalid_argument，不隐式推断派生转换。
`reset_reference()` 释放当前引用，不影响其他强引用；视图不会自动延长目标寿命，
重置/替换后应重新获取目标视图。修改目标会被所有共享拥有者观察到，不提供写时复制。

默认反射 JSON 支持空引用和纯反射对象的内嵌引用。非空引用输出 `$root`/`$objects`
封装；对象表条目保存 `$type_guid` 与 `$content`，属性保存 `$ref`。这些名称属于文档协议，
根对象不要使用 `$objects` 作为普通属性名。无非空引用时保留普通 JSON 布局。

同一次编解码内相同目标共享对象；独立加载形成独立对象图。加载按需构造条目，正在构造的
对象不允许被链接，从而拒绝强引用环并避免失败时残留环。重复/未知 ID、不可达条目、
类型不匹配均失败；最多 10000 个内嵌对象、64 层嵌套。空引用保存为 null。

非空目标必须为注册的纯反射对象（具有构造器且未注册自定义 encode/decode）；
跨静态类型、已检测到的控制块别名不支持。调用方不得将成员别名作为独立持久化对象。
自定义 codec 自行调用序列化时建立自己的上下文，尚不支持跨 codec 共享身份；
外部 URI 引用和 Inspector 引用选择控件仍待 PLAN-093 后续阶段接入。

### 跨进程组件描述

Meta 注册表仅属于当前进程。通过 IPC 接收场景组件描述的宿主，也必须在解码前完成
`initialize_meta`；启动的 Runtime 子进程初始化不能替代 Editor 接收端初始化。
独立运行的 IPC 集成测试同样需要遵循此启动约定。协议解码本身不隐式初始化或冻结注册表。
