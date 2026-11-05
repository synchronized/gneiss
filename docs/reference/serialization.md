<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 序列化接口

实现位于 `src/engine/core/serialization`，使用 Meta 的类型和属性访问，不维护第二套注册表。

## 调用入口

普通 JSON 使用 `json.hpp`：

```cpp
namespace ser = gneiss::engine::serialization;
auto text = ser::json::serialize(description);
auto bytes = ser::json::serialize_bytes(description);
auto restored = ser::json::deserialize<scene_description>(json_text);
```

使用其他后端时包含 `serialization.hpp`，传入实现了 reader/writer 的对象：

```cpp
auto status = ser::serialize(description, output_writer);
auto restored = ser::deserialize<scene_description>(input_reader);
```

写入返回 `serialization::diagnostic`，其 error 为 success 时成功；
构造对象的读取返回 `result<T, serialization::diagnostic>`。
所有显式上下文重载采用 context-first：

```cpp
ser::context ctx;                         // 使用默认注册表
ser::context isolated{registry, 64};      // 指定注册表与最大嵌套深度
auto text = ser::json::serialize(ctx, description);
auto restored = ser::json::deserialize<scene_description>(ctx, json_text);
auto status = ser::serialize(ctx, description, output_writer);
```

无 context 的类型模板使用默认注册表；视图重载使用视图绑定的注册表。
显式 context 与视图必须绑定同一注册表，否则返回 invalid_argument，不修改读取目标。
显式空注册表返回 invalid_handle，不回退默认表；context 不自动注册或冻结类型。
无效输入、未注册类型及后端错误不返回半成品对象；写入已有 mutable_instance 的重载不承诺回滚。

## 共享遍历

`traversal.cpp` 负责字段遍历、枚举名称、sequence、associative、optional、variant、
内嵌引用与多态类型信封、引用环/深度检查、构造与校验、字段错误路径。
它不包含 yyjson，也不选择 JSON codec。

类型通过 `.serialization()` 启用共享字段遍历；字段开关和未知字段策略属于通用绑定。
特殊类型通过 `.serialization<Encode, Decode>()` 注册通用操作，按方向优先于字段遍历。
编码签名为 `(const context&, writer&, const T&) -> result<writer::node, diagnostic>`；
解码签名为 `(const context&, reader&, reader::node) -> result<T, diagnostic>`。
允许 nullptr 表示缺少某方向，该方向若启用字段遍历则回退，否则返回 unsupported。
自定义编码返回对象节点，解码返回独立对象；异常由序列化边界转换为错误。
Meta 不包含 JSON 类型或格式专用回调，reader/writer 不承担类型 codec 注册。
value 通过 variant_traits 接入通用 variant 遍历，对象分支复用通用多态引用，无专用 codec。

## 后端契约

reader/writer 提供对象、数组、空值、字符串及基础值操作。
节点句柄是不透明借用值，只能交回所属后端；null 句柄表示不存在或创建失败，
文件中的空值仍由有效节点表示。writer 创建的节点由 writer 后端管理。
reader 的键名和游标借用输入，不能超过输入及 reader 的生命周期。
context 的配置只读，递归状态私有；即使参数是 const 引用，也不能并发共用同一 context。
context 不可复制，可在操作完成或失败后顺序复用；外层入口退出会清理深度、循环引用及 reader 状态。
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

普通 `serialization::json::serialize(description)` 只保存描述内容，不添加文件头。
完整资源文件包含 `engine/resource/asset_file.hpp`：

```cpp
auto bytes = asset_file::serialize_bytes(scene);
auto loaded = asset_file::deserialize<scene_description>(file_bytes);
```

资源入口提供 context-first 及默认上下文版本；`format_traits<T>` 选择文件格式。
Material、Action Map、Scene、Prefab、Texture 的格式名、版本、大小限制集中在 asset_file.cpp。
读写复用反射字段与校验，文件头只作用于最外层，不传播到嵌套描述对象。
错误版本、重复格式头、超出文件大小及已移除字段由文件边界拒绝；普通对象没有这些文件约束。
导入器输出、编辑器保存和内置资源的 asset_decoder 默认路径使用文件入口。
未知文件类型仍可给 asset_manager 显式注册字节解码函数，不在 Meta 注册文件协议。

## 定义归属

错误值为 `serialization::diagnostic`，定义在 `diagnostic.hpp`；
类型的编解码规则为 `serialization::binding`，定义在 `binding.hpp`。
类型构造与继承转换属于 Meta 的 object_ops，不存放于编解码绑定。
内建反射对象根类型的注册实现在 `meta/builtin_registration.cpp`；value 是内建 variant 包装，
无需登记自己的字段或 codec。JSON 节点适配位于 `detail/json_adapter.hpp`，
命名空间为 `serialization::json::detail`；普通调用方使用文本、字节或 reader/writer 入口。
