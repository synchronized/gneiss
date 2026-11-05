<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 引擎通用值

`engine/core/value.hpp` 定义 `gneiss::engine::value` 与 `value_kind`。它们不依赖反射注册表，
用于反射读写和组件属性传递；其中标量分支可用于编辑器快照和撤销记录。

`value::payload` 为 `storage_type`（std::variant），支持空值、bool、int64、uint64、float、double、
std::string、guid、vector2、vector3、vector4、quaternion、ref<meta::object>。`kind()` 返回当前类别，默认空值及异常无值状态返回 invalid。
稳定 GUID 直接存储 `engine::guid`，对应 `value_kind::type_guid`（保留既有语义类别名），类别数值仍为 7；其他类别序号也保持不变。

字符串由值持有，复制后不依赖原对象或注册表；修改副本不改变原字符串。构造和复制可能抛出分配异常，
移动不复制字符串。读取字段时复制文本，写入字段时传 const value&；调用方不得保存输入引用超过调用期。

通用值不校验 UTF-8、内嵌零或浮点有限性，这些规则由反射、资产和 IPC 的具体边界执行。
它不是可存任意 C++ 结构体的 any，也不持有借用对象指针或容器；结构体仍通过 meta::instance 访问。

IPC 属性载荷与编辑器属性模型直接使用 value，不再维护 ipc_property_value/runtime_property_value。
协议编码仍显式处理类型标签，不直接序列化 std::variant 或 GLM 内存。旧标量标签保持不变：
IPC 的 GUID 仍使用 type_id 标签，且必须非 nil；文件中的 GUID 仍使用 guid 标签。
IPC 补充 vec2、vec4 与 object 标签；旧接收端不认识新标签时拒绝该消息。
字符串和消息大小限制仍由 IPC 入口执行。

## 对象强引用

object 分支的类别值为 12，保存 ref<meta::object>。value 头仅前置声明该基类；
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

对象分支使用以下形式，空引用为 {"kind":"object","value":null}：

```json
{"kind":"object","value":{"$type_guid":"<已注册类型 GUID>","$content":{"speed":3}}}
```

实际类型必须已注册、派生自 meta::object 并具备相应编解码能力。Builder 在编译期建立安全转换，
解码前检查类型资格，解码后验证动态身份。未知类型返回 not_found，普通结构体或缺少 codec 返回
unsupported；引用环、非法信封、深度超限及对象业务校验失败返回相应错误。
通用 value codec 通过 reader/writer 使用当前注册表与递归上下文，不依赖 JSON，也不重置引用环状态。

解码生成独立对象，重复引用也分别恢复，不跨进程保持地址或共享身份。
对象分支的 IPC 编解码要求先完成默认 Meta 注册与冻结；标量 IPC 不增加该前置条件。
IPC 传递的是对象内容，不是远端对象句柄；运行对象定位继续使用对象 ID，资源身份继续使用 URI。
序列化能力不代表 Runtime 所有属性均可写，对应组件适配器仍决定写入能力。
普通类型化 ref 的内联多态持久化规则不变。

## JSON 与组件属性入口

IPC 使用 serialization::json::read_value/write_value 复用 Meta 标签编解码。
协议层只保留大小限制及 GUID 的 type_id/32 位十六进制适配，不再重复标量分派。
该入口的标量分支不要求冻结注册表；对象沿用已注册类型和递归校验规则。

read_component_property 返回 result<value>，不再带输出参数。
Transform 和普通组件的默认字段访问统一使用 meta::instance；Transform 的 World 存储、
适配器的捕获/安装和显式字段映射保留各自职责。

对象引用面板支持展开、编辑内部属性、复制内容、粘贴替换和清空。
ref<const T> 仅展示。嵌套编辑提交独立对象副本，不直接修改原引用；
Runtime 面板发送整项对象内容，由适配器检查和安装。
Prefab 标量覆盖不自动升级为对象覆盖，资源 URI 继续走资源编辑流程。
