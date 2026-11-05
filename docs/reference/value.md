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

资产和 IPC 的专用载荷仍保留协议数组、固定 GUID 字节等表示，显式转换到 value；不直接序列化
std::variant 或 GLM 对象内存。文件与 IPC 的现有字段名及编码不变。

## 对象强引用

object 分支的类别值为 12，保存 ref<meta::object>。value 头仅前置声明该基类；
创建具体对象时包含 meta/object.hpp，复用现有对象动态身份。

复制 value 共享控制块和对象，比较按对象指针身份，不进行深拷贝或内容比较。
空对象引用仍是 object 分支，区别于默认 invalid value。对象改变会被所有引用观察到；
引用计数不保证对象数据线程安全，也不回收强引用环。
最后一次释放使用原 ref 删除器，不额外安排线程。

反射引用属性的读写方式见[反射参考](reflection.md#value-对象引用)。
valid_scalar 对对象分支返回 false。对象引用不能直接充当撤销或 Prefab 覆盖快照；
需要快照时业务显式复制对象或捕获属性数据。

通用 value JSON 对对象分支（包括空引用）返回 unsupported，IPC 也不传输进程内引用。
普通类型化 ref 的内联多态持久化规则不变，资源身份仍使用 URI。
