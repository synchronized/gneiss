<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Type Registry 与反射元数据

## 当前范围

`<gneiss/reflection.h>` 提供 C11 Type Registry 和稳定元数据查询；`<gneiss/reflection.hpp>` 提供
独占 RAII 包装。当前接口描述类型和字段，并通过显式绑定的适配器提供类型安全属性读写；尚不提供
对象构造、ECS 组件访问、继承或序列化迁移。

## 稳定标识

`gneiss_type_id` 是 16 字节 Type ID，全零值无效。`gneiss_field_id` 是类型内部的非零 `uint32_t`。
字段完整身份由 Type ID 与 Field ID 共同组成。名称只用于 UTF-8 显示和诊断，不参与身份判断。
Type ID 字节顺序与规范 UUID 文本去掉连字符后的十六进制字节顺序一致，不按主机端序解释。

`gneiss_field_desc.value_type_id` 表示字段值的语义 Type ID；字段值类型不要求在同一个 Registry 中
预先注册。`GNEISS_FIELD_FLAG_READ_ONLY` 禁止为字段绑定 setter。

## 属性值与访问器

`gneiss_property_value` 是调用方拥有的带类别值容器，首版支持布尔、`int64`、`uint64`、`float`、
`double`、UTF-8 字符串、Type ID、三维向量和四元数。布尔值只能为 0 或 1；浮点和数学值必须有限；
字符串不得包含内嵌空字符并必须是合法 UTF-8；Type ID 不得全零。通用层不负责归一化四元数或字段
专属范围约束，这些约束由 setter 在修改目标前完整验证。

`gneiss_type_registry_bind_property` 在冻结前将 getter/setter 和属性类别绑定到已注册字段。绑定不改变
字段身份；完整相同的重复绑定幂等成功，冲突绑定返回参数错误。`gneiss_field_info` 在冻结后公开属性
类别及可读、可写能力。没有绑定或缺少对应方向的访问器返回 `GNEISS_ERROR_UNSUPPORTED`。

`gneiss_property_target` 包含两个不透明 `uint64_t`，其含义由适配器定义。Registry 不解释也不持有
目标；后续 ECS 适配将分别承载 World 和 Entity 身份。`user_data`、回调代码及回调返回的字符串均为
借用对象：前两者至少保持到 Registry 销毁，字符串有效期由具体适配器约定，调用方需要长期保存时
必须复制。setter 收到的字符串只保证在调用期间有效。

getter 必须填写与绑定类别一致且合法的值；违反契约返回 `GNEISS_ERROR_INTERNAL` 并清空输出。setter
必须先完成全部校验再修改目标，失败时保持目标原值。回调不得让异常穿过 ABI；实现也会将意外异常
转换为 `GNEISS_ERROR_INTERNAL`。

## 生命周期

1. `gneiss_type_registry_create` 创建空 Registry。
2. 初始化线程注册类型，并按需绑定属性访问器。
3. `gneiss_type_registry_freeze` 完成排序并禁止后续注册和绑定。
4. 冻结后通过数量、索引、Type ID 或 Field ID 查询元数据。
5. `gneiss_type_registry_destroy` 销毁 Registry，所有借用元数据立即失效。

注册会深拷贝类型名、字段名和描述。相同 Type ID 与完整描述的重复注册幂等成功；Schema 版本、
名称、字段集合或字段描述不一致均返回 `GNEISS_ERROR_INVALID_ARGUMENT`。重复冻结幂等成功，冻结后
注册返回 `GNEISS_ERROR_INVALID_STATE`。

查询只允许在冻结后进行，冻结前返回 `GNEISS_ERROR_NOT_READY`。未知类型、字段或越界索引返回
`GNEISS_ERROR_NOT_FOUND`。空输出指针、全零 Type ID、零 Field ID 和无效描述返回
`GNEISS_ERROR_INVALID_ARGUMENT`；失效或重复销毁的 Registry 返回 `GNEISS_ERROR_INVALID_HANDLE`。

## 所有权与线程安全

`gneiss_type_info`、`gneiss_field_info` 及其字符串和字段数组均由 Registry 持有。调用方不得释放或
修改；它们在 Registry 销毁前保持有效。销毁必须与其他访问进行外部同步。

创建和销毁不同 Registry 可以并发进行。单个 Registry 的注册与绑定阶段由调用方串行化；冻结后
Registry 查询和访问器查找支持并发调用。目标对象是否支持并发读写由具体适配器决定。首版不支持
注销、解冻、替换描述和动态模块卸载。

## C++20 包装

`gneiss::type_registry` 独占 C 句柄，不可复制、可以移动，析构时自动销毁。`create`、`register_type`、
`bind_property`、`freeze`、查询、`get_property` 和 `set_property` 直接返回 `gneiss::result`，不建立
第二套元数据或属性状态。
