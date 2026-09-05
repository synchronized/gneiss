<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Core 版本与结果接口

## 版本

构建生成的 `<gneiss/core/version.h>` 提供编译期版本宏：

- `GNEISS_VERSION_MAJOR`
- `GNEISS_VERSION_MINOR`
- `GNEISS_VERSION_PATCH`
- `GNEISS_VERSION_STRING`

C API 的 `gneiss_version_major`、`gneiss_version_minor` 和 `gneiss_version_patch` 返回运行时库版本。
C++ API 使用 `gneiss::library_version()` 返回强类型版本结构。使用共享库时可以比较编译期和运行时
版本，诊断加载了错误运行库的问题。

## 结果码

`gneiss_result` 是 32 位有符号整数。零表示成功，负值表示失败；调用方不得自行解释尚未定义的
数值。

| 结果码 | 含义 |
| --- | --- |
| `GNEISS_SUCCESS` | 操作成功 |
| `GNEISS_ERROR_UNKNOWN` | 未分类错误 |
| `GNEISS_ERROR_INVALID_ARGUMENT` | 参数不满足接口契约 |
| `GNEISS_ERROR_INVALID_HANDLE` | 句柄无效、类型错误或已经失效 |
| `GNEISS_ERROR_OUT_OF_MEMORY` | 无法完成必要内存分配 |
| `GNEISS_ERROR_UNSUPPORTED` | 当前实现或平台不支持该操作 |
| `GNEISS_ERROR_INITIALIZATION_FAILED` | Gneiss 对象或服务初始化失败 |
| `GNEISS_ERROR_DEPENDENCY_FAILED` | Granit 等依赖调用失败 |
| `GNEISS_ERROR_INVALID_STATE` | 当前生命周期状态不允许该操作 |
| `GNEISS_ERROR_NOT_READY` | 操作暂时无法完成，可以稍后重试 |
| `GNEISS_ERROR_INTERNAL` | 内部不变量被破坏或发生未分类内部错误 |

`gneiss_result_message` 返回由运行库持有的静态英文文本，线程安全且不返回空指针。未知数值返回
`unrecognized result`。

C++ API 使用只保存一个 32 位结果码的 `gneiss::result` 轻量值类型。它提供 `ok()`、`failed()`、
`native()`、`message()` 和显式 `operator bool()`；布尔上下文中成功为 `true`，失败为 `false`。
默认构造产生 `unknown` 失败结果，避免尚未赋值的结果静默表示成功；任意 C 结果码可通过显式构造
或 `from_native` 无损保留。

```cpp
const auto operation = application.run();
if (!operation) {
  std::cerr << operation.message();
}
```

- `gneiss::to_native` 与 `gneiss::from_native` 转换 C/C++ 表达。

结果码只表达稳定分类。更详细的后端错误、对象和调用上下文应由后续诊断接口提供，不能通过持续
增加高度具体的公共结果码替代诊断系统。
