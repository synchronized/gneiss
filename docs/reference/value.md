<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 引擎通用值

`engine/core/value.hpp` 定义 `gneiss::engine::value` 与 `value_kind`。它们不依赖反射注册表，
用于反射读写、组件属性、编辑器快照和撤销记录。

`value::payload` 为 `storage_type`（std::variant），支持空值、bool、int64、uint64、float、double、
std::string、type_guid、vector3、quaternion。`kind()` 返回当前类别，默认空值及异常无值状态返回 invalid。
稳定 GUID 对应 `value_kind::type_guid`，类别数值仍为 7；其他类别序号也保持不变。

字符串由值持有，复制后不依赖原对象或注册表；修改副本不改变原字符串。构造和复制可能抛出分配异常，
移动不复制字符串。读取字段时复制文本，写入字段时传 const value&；调用方不得保存输入引用超过调用期。

通用值不校验 UTF-8、内嵌零或浮点有限性，这些规则由反射、资产和 IPC 的具体边界执行。
它不是可存任意 C++ 对象的 any，也不持有借用对象指针或容器。

资产和 IPC 的专用载荷仍保留协议数组、固定 GUID 字节等表示，显式转换到 value；不直接序列化
std::variant 或 GLM 对象内存。文件与 IPC 的现有字段名及编码不变。
