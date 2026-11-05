<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 引擎通用值

`engine/core/value.hpp` 定义 `gneiss::engine::value` 与 `value_kind`。它们不依赖反射注册表，
用于反射读写和组件属性传递；其中标量分支可用于编辑器快照和撤销记录。

`value::payload` 为 `storage_type`（std::variant），支持空值、bool、int64、uint64、float、double、
std::string、guid、vector2、vector3、vector4、quaternion、matrix3、matrix4、ref<meta::object>。`kind()` 返回当前类别，默认空值及异常无值状态返回 invalid。
GUID 对应 value_kind::guid（7）；数学类别为 20–25，对象为 50。kind_index 显式映射存储下标，
类别数值不用于文件标签或类型遍历。

字符串由值持有，复制后不依赖原对象或注册表；修改副本不改变原字符串。构造和复制可能抛出分配异常，
移动不复制字符串。读取字段时复制文本，写入字段时传 const value&；调用方不得保存输入引用超过调用期。

通用值不校验 UTF-8、内嵌零或浮点有限性，这些规则由反射、资产和 IPC 的具体边界执行。
它不是可存任意 C++ 结构体的 any，也不持有借用对象指针或容器；结构体仍通过 meta::instance 访问。

IPC 属性载荷与编辑器属性模型直接使用 value，不再维护 ipc_property_value/runtime_property_value。
文件和 IPC 均通过通用 variant 遍历保存 `$kind/$content`，不保存 std::variant 或 GLM 原始内存。
删除旧 kind/value 和 IPC GUID 值的 type_id 标签适配；GUID 内容为规范 UUID 或 null。
字符串和消息大小限制仍由 IPC 入口执行。

## 对象强引用

object 分支的类别值为 50，保存 ref<meta::object>。value 头仅前置声明该基类；
创建具体对象时包含 meta/object.hpp，复用现有对象动态身份。

复制 value 共享控制块和对象，比较按对象指针身份，不进行深拷贝或内容比较。
空对象引用仍是 object 分支，区别于默认 invalid value。对象改变会被所有引用观察到；
引用计数不保证对象数据线程安全，也不回收强引用环。
最后一次释放使用原 ref 删除器，不额外安排线程。

反射引用属性的读写方式见[反射参考](reflection.md#value-对象引用)。
valid_scalar 对对象分支返回 false。对象引用不能直接充当撤销或 Prefab 覆盖快照；
需要快照时业务显式复制对象或捕获属性数据。组件编辑已经采用序列化内容快照，
每次撤销/重做重新解码；引用面板的内部修改先复制对象，再产生整项替换操作。

## 对象内容编解码

对象分支使用以下形式，空引用为 {"$kind":"object","$content":null}：

```json
{"$kind":"object","$content":{"$type_guid":"<已注册类型 GUID>","$content":{"speed":3}}}
```

实际类型必须已注册、派生自 meta::object 并具备相应编解码能力。Builder 在编译期建立安全转换，
解码前检查类型资格，解码后验证动态身份。未知类型返回 not_found，普通结构体或缺少 codec 返回
unsupported；引用环、非法信封、深度超限及对象业务校验失败返回相应错误。
通用 variant 与引用遍历通过 reader/writer 使用当前注册表与递归上下文，无专用 value codec。

解码生成独立对象，重复引用也分别恢复，不跨进程保持地址或共享身份。
对象分支的 IPC 编解码要求先完成默认 Meta 注册与冻结；标量 IPC 不增加该前置条件。
IPC 传递的是对象内容，不是远端对象句柄；运行对象定位继续使用对象 ID，资源身份继续使用 URI。
序列化能力不代表 Runtime 所有属性均可写，对应组件适配器仍决定写入能力。
普通类型化 ref 的内联多态持久化规则不变。

## JSON 与组件属性入口

IPC 使用 serialization::json::read_value/write_value 复用 Meta 标签编解码。
协议层直接嵌入完整值节点，只保留大小限制；命令 type_id 使用 guid 的标准带连字符字符串，不再重复标量分派。
该入口的标量分支不要求冻结注册表；对象沿用已注册类型和递归校验规则。

read_component_property 返回 result<value>，不再带输出参数。
Transform 和普通组件的默认字段访问统一使用 meta::instance；Transform 的 World 存储、
适配器的捕获/安装和显式字段映射保留各自职责。

对象引用面板支持展开、编辑内部属性、复制内容、粘贴替换和清空。
ref<const T> 仅展示。嵌套编辑提交独立对象副本，不直接修改原引用；
Runtime 面板发送整项对象内容，由适配器检查和安装。
Prefab 标量覆盖不自动升级为对象覆盖，资源 URI 继续走资源编辑流程。

## Variant 访问与数学值

Meta 的 variant_traits<value> 暴露 payload 存储；value 本身不依赖 Meta 操作表。
instance.as_variant() 可查询分支、借用内容，mutable_variant_view.select() 默认构造所选分支。
切换分支后旧内容视图失效。value 的 read_value/write_value 仍提供整项复制和替换。
分支标签由 variant_case_traits 显式定义，不使用枚举数字或编译器字符串。

默认空值保存为 `{"$kind":"invalid","$content":null}`；异常无值状态不能保存。
空对象保存为 `{"$kind":"object","$content":null}`，两者不混同。
矩阵分别保存列优先的 9/16 个有限 float 分量；读取严格检查长度、数值范围和非有限值。
编辑器矩阵当前只读；内存 API 与序列化支持读写。
