<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Core 版本与结果接口

## 版本

仓内使用 `gneiss::engine::library_version()` 返回运行时库版本，
`library_version_string()` 返回静态存储的版本字符串。声明位于
`src/engine/core/version.hpp`。构建版本头仅供内部实现与版本用例使用，不安装。
游戏模块协议已撤销，不再发布模块 ABI 版本。

## 结果码

仓内操作统一使用 `gneiss::engine::result`（`engine/core/result.hpp`）。它只保存一个
32 位结果码，提供 `ok()`、`failed()`、`code()`、`message()` 和显式布尔判断。
默认构造产生 unknown；非负值成功，负值失败，不提供隐式整数转换。
`message()` 返回静态英文文本的 string_view，未知数值返回 `unrecognized result`。

普通内部调用直接传递该值；游戏模块撤销后，C 结果转换辅助也已删除。
整数结果可通过显式构造与 code() 无损读写。旧 gneiss::result 别名不再保留。

## 内部符号可见性

共享库的导入/导出宏定义在 `src/engine/core/export.hpp`，由受限内部头清单提供给仓内目标。
公共 include 目录和 C 结果码副本已删除，不再安装头文件；需要新的对外接口时另行设计。

## 内部基础身份类型

以下类型位于 `src/engine/core`，命名空间为 `gneiss::engine`，不属于公共 C ABI。
来源及许可见 [第三方声明](../../THIRD_PARTY_NOTICES.md)。

- `string_name`：驻留字符串，相同文本共享稳定条目；空串和空指针生成无效值。
  `find` 只查询，不创建；空串查询返回空 optional。比较与哈希使用条目地址，不能写入存档。
  Engine 内只有一张带锁字符串表，不逐条回收。`view()` 借用表内文本，所有引用必须在
  Engine 卸载前停止使用；构造与查询线程安全，可能抛出分配异常。
- `guid`：16 字节值，默认 nil。解析严格的 36 字符 UUID 文本，接受大小写十六进制，输出小写；
  不提供随机生成，也不替换当前场景 UUID 字符串。
- `type_guid`：对 GUID 的类型标识包装，用于显式约定的稳定类型身份；不从地址或名称哈希推导。
- `core/traits` 下的 `type_name<T>` 与 `type_identity<T>`：名称与稳定 GUID 定制点。内置类型提供名称，默认 GUID
  为 nil；未定制用户类型名称为空。可显式特化 `type_identity<T>`；泛型调用方应先用
  `std::remove_cvref_t<T>` 归一化，再访问用户特化。

- `type_id`：Engine 内集中分配的运行期类型键。`runtime_type_of<T>()` 去除顶层 cv/ref，
  首次按工具链规范类型名注册，之后缓存编号；void 返回无效 ID。编译器类型名仅作为内部去重键，
  不提供按 ID 查询名称的接口，也不作为展示名称或持久化契约。
  资源缓存与 Loader 使用此键，不使用名称 hash 代替身份。首次初始化分配失败不可恢复。

`type_id` 不持久化，不替代反射的 16 字节 `type_guid`、稳定 GUID 或资产 UUID。
跨模块使用要求相同工具链/ABI、满足 ODR 的具名类型，并链接同一个 Shared Engine。
静态构建只保证同一链接映像内统一，匿名/局部类型不作为跨模块资源契约。
不提供类型擦除对象的析构回调或字段注册；具体资源仍由已有 `asset_ref` 与 Loader 管理。
见 [ADR-081](../decisions/ADR-081-runtime-type-identity.md)。
编译消费者应链接 Engine，不能把 string_name.cpp 各自复制进插件生成独立的字符串表。
