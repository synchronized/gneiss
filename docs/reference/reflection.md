<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Type Registry 与反射元数据

## 当前范围

反射注册与查询使用 `src/engine/core/reflection/type_registry.hpp` 和
`property_registry.hpp` 中的原生接口。Registry 的 C 函数、公共句柄表及 C 元数据缓存已删除。
旧 `reflection.h`、C 属性值布局和转换头已删除；Editor/Runtime 属性命令直接持有原生 `type_guid`。
IPC 仍使用既有 32 位十六进制字符串编码 GUID，不传输原生对象布局。

当前支持类型、字段和显式属性访问器，内建 Transform 与 Camera 已接入；不提供对象构造、
继承、注销、解冻、描述替换或动态模块卸载。

## 身份与注册

`engine::type_guid` 保存 16 字节稳定类型身份，全零无效；`engine::type_id` 是进程内类型编号，
不用于反射存档。字段完整身份由稳定 GUID 与非零字段编号组成。名称用于 UTF-8 显示和诊断。
GUID 字节顺序与规范 UUID 文本去掉连字符后的顺序一致，不按主机端序解释。

`field_definition.type_guid` 表示字段值的语义类型，无需事先注册该类型。
`field_flags::read_only` 禁止为字段绑定 setter。

`create_native` 创建 `registry_ref` 所有者；`register_type` 深拷贝名称和字段描述。
类型自身的稳定身份为 `type_definition.guid`、`type_info.guid`；注册输入另有可选的运行期 `type_id`。
重复 GUID 或重复非空运行期 ID 的注册返回 `result::invalid_argument`，不覆盖已有记录。
注册名称不得为空或包含内嵌空字符，必须是合法 UTF-8；重复字段编号或名称也会被拒绝。

`bind_property` 绑定原生函数指针、类别和借用上下文；相同完整绑定可重复调用。
`freeze` 按 GUID 字节顺序和字段编号排序，之后禁止注册与绑定。重复冻结幂等成功。

## 查询与属性值

`type_count`、`describe_type_at` 枚举冻结元数据；`describe_type`、`describe_field` 直接查询记录。
`describe_type` 支持稳定 GUID 和运行期 `type_id` 两种入口，指向同一份记录。
`register_type<T>` 填充并校验 C++ 类型身份；`describe_type<T>` 使用 `runtime_type_of<T>()` 查询。
模板去除顶层 cv/ref；注册 `void` 或传入不匹配的非空 `type_id` 返回 `invalid_argument`。
不对应 C++ 类型的描述保留空 `type_id`，仍可按 GUID 查询。运行期 ID 不进入文件或 IPC。
`field_definition` 的编号和标志分别使用 `field_id` 与 `field_flags`；`bind_property` 接收强类型字段编号。
查询失败保持输出；未冻结返回 `not_ready`，未知身份或越界索引返回 `not_found`，空引用返回
`invalid_handle`。类型查询输出拥有字段数组，名称借用 Registry，不能超过 Registry 生命周期。

原生属性值支持布尔、int64、uint64、float、double、UTF-8 字符串、稳定 GUID、三维向量和四元数。
浮点与数学值必须有限，字符串必须为无内嵌空字符的合法 UTF-8，GUID 不得全零。
字段专属范围和四元数归一化由 setter 校验，失败不得留下部分修改。

回调及底层 `get_property`、`set_property` 使用 `gneiss::property_value`，其中字符串为借用视图。
getter 返回的文本至少存活到调用方完成复制，setter 输入仅在调用期间有效。
`read_property` 返回拥有文本的 `engine::property_value` 快照；`write_property` 在调用期间借用
自有快照的文本。两个高层入口失败保持调用方输出和既有目标语义。

未绑定字段或缺少对应方向的访问器返回 `unsupported`。getter 抛异常或返回非法类别/值时返回
`internal` 并清空底层输出；自有快照查询保持原输出。setter 异常也转换为 `internal`。
基础 getter 接收 `const void* object`，setter 接收 `void* object`。对象仅在调用期间借用，Registry
不复制、不持有对象，也不解释 World/Entity 标识。调用方必须保证对象实际类型、寿命及线程访问合法。
空对象返回 `invalid_argument`；不支持运行期校验任意 void* 的实际类型。
setter 应先校验再修改，Core 不回滚第三方回调已经产生的副作用。Prefab 临时数据使用同一 setter，
不再提供独立的候选访问接口；跨字段联合校验由提交数据的调用方负责。

## 内建组件与生命周期

`register_reflection` 为未冻结 Registry 注册 Transform 和 Camera；重复调用返回 `invalid_argument`。
`create_property_registry` 直接创建已注册且冻结的内建 Registry。
稳定身份使用 `transform_component_type`、`camera_component_type` 或对应原生查询函数。

`world_reflection.hpp` 提供 `read_component_property`、`write_component_property`：显式接收 World RID、
Entity、类型 GUID 和字段 ID。先通过 World 接口校验线程和实体并读出组件副本，基础反射操作副本，
写成功后通过既有 World 接口提交，以保留联合校验、Transform 传播和既有更新行为。
Editor、Runtime 和运行时 Prefab 覆盖使用这些组件入口；Core 不依赖 World。

Transform 的反射对象为 `transform`；Camera 为 World 层的 `camera_properties`，包含
`camera_settings settings` 和只读 `bool is_primary`。活动状态由 World 读出，不在基础 getter 中查询。
Camera GUID 和字段编号不变，C++ 模板查询使用 `describe_type<camera_properties>`。
Transform 字段为平移、旋转、缩放；Camera 字段为视场角、近裁剪面、远裁剪面与只读活动状态。

Registry 使用原生共享引用管理，最后一个引用释放时销毁，不经全局句柄表。
场景服务和场景实例持有引用，Prefab 操作在调用期间借用。
元数据与回调上下文的使用必须在相应所有者销毁前结束；Registry 不拥有回调上下文。

初始化阶段由调用方串行注册和绑定；冻结后的查询与访问器查找可以并发调用。
目标对象的线程规则由访问器决定，内建 World 操作仅允许所属线程调用。
