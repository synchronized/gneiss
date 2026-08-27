<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Type Registry 与反射元数据

## 当前范围

`<gneiss/reflection.h>` 提供 C11 Type Registry 和稳定元数据查询；`<gneiss/reflection.hpp>` 提供
独占 RAII 包装。当前接口只描述类型和字段，不提供对象构造、属性值读写、ECS 组件访问、继承或
序列化迁移。这些能力属于 0.6.0 后续任务。

## 稳定标识

`gneiss_type_id` 是 16 字节 Type ID，全零值无效。`gneiss_field_id` 是类型内部的非零 `uint32_t`。
字段完整身份由 Type ID 与 Field ID 共同组成。名称只用于 UTF-8 显示和诊断，不参与身份判断。
Type ID 字节顺序与规范 UUID 文本去掉连字符后的十六进制字节顺序一致，不按主机端序解释。

`gneiss_field_desc.value_type_id` 表示字段值的 Type ID；字段值类型不要求在同一个 Registry 中预先
注册，因此可按模块分阶段组装描述。`GNEISS_FIELD_FLAG_READ_ONLY` 表示后续属性接口不得写入字段。

## 生命周期

1. `gneiss_type_registry_create` 创建空 Registry。
2. 初始化线程通过 `gneiss_type_registry_register` 注册类型。
3. `gneiss_type_registry_freeze` 完成排序并禁止后续注册。
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

创建和销毁不同 Registry 可以并发进行。单个 Registry 的注册阶段由调用方串行化；冻结后所有查询
接口支持并发调用。首版不支持注销、解冻、替换描述和动态模块卸载。

## C++20 包装

`gneiss::type_registry` 独占 C 句柄，不可复制、可以移动，析构时自动销毁。`create`、`register_type`、
`freeze`、`type_count`、`find_type` 和 `find_field` 直接返回 `gneiss::result`，不建立第二套元数据状态。
