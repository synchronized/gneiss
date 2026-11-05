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

写入返回 `meta::serialization_diagnostic`，其 error 为 success 时成功；
构造对象的读取返回 `result<T, meta::serialization_diagnostic>`。
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
`.json_serialization<Encode, Decode>()` 与 `.json_bytes_serialization<Encode, Decode>()`
仍登记 JSON 专用回调；JSON 后端按方向优先使用它们。
仅登记 JSON codec 的类型不会自动获得其他格式的编解码能力。

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
JSON 节点回调接受 `(const context&, doc, object)` / `(const context&, node)`；
字段委托使用 `json::write_fields(ctx, doc, object)` / `json::read_fields<T>(ctx, node)`。
JSON 字节回调可接收 context-first 参数，也保留不接收 context 的纯转换函数适配。
回调只借用 context，不得在返回后保存其引用。

JSON 后端直接借用 yyjson 节点，不构建中间 DOM，不为共享遍历重新解析文本。
reader 的容器游标逐项前进，不额外分配整个节点列表。
writer 的结构操作返回 false 表示创建/挂接失败，统一转换为 out_of_memory；
scalar 和格式专用扩展可返回带诊断的错误。
失败后的 writer 可能包含尚未设置为根的临时节点，其回收由后端负责。

当前接口面向内存中的结构编码，并非前向流式 writer。
没有通用二进制后端或 MessagePack 实现；Mesh/Texture 的专用二进制格式继续使用独立 codec。
设计取舍见 [ADR-105](../decisions/ADR-105-serialization-format-boundary.md)。
