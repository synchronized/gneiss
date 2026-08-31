<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Game Module ABI 与生命周期

## 稳定性

`<gneiss/game_module.h>` 提供 0.12.0 开始使用的 Experimental 原生 Game Module C ABI。该接口尚未
进入 Stable 兼容承诺；升级 Gneiss 后应重新编译模块，并在加载时校验 ABI 版本。

当前版本只定义模块契约和描述校验。动态库加载、Runtime 生命周期调度和 Game Context 访问能力将在
0.12.0 后续里程碑接入，不能把声明的回调误认为 Runtime 已经开始调用。

## 查询入口

每个原生模块必须导出 `gneiss_game_module_query`。Runtime 将使用
`GNEISS_GAME_MODULE_QUERY_SYMBOL` 查找该符号，并以
`GNEISS_GAME_MODULE_ABI_VERSION_CURRENT` 调用。模块应在调用方提供的结构大小内填写
`gneiss_game_module_desc`，不得保存输出指针。

构建模块时定义 `GNEISS_BUILDING_GAME_MODULE`，使查询入口在 Windows 使用 `dllexport`，在支持符号
可见性的编译器上使用默认可见性。查询入口本身由模块实现，不由 `gneiss_engine` 导出。

## 描述结构

`gneiss_game_module_desc` 包含：

- `struct_size`：调用方可写结构大小，首版最小值为 `GNEISS_GAME_MODULE_DESC_VERSION_1_SIZE`。
- `abi_version`：必须等于当前 Game Module ABI 版本。
- `module_id` 与 `module_id_length`：非空 UTF-8 稳定标识；字符串由模块持有。
- `initialize`、`fixed_update`、`update`、`shutdown`：首版全部必需。
- `reserved`：必须清零。

`gneiss_game_module_validate` 校验上述约束，不取得描述、字符串或回调所有权。校验失败返回
`GNEISS_ERROR_INVALID_ARGUMENT`。

## 生命周期与所有权

Runtime 计划按以下顺序调用模块：

1. `initialize` 借用非零 Game Context，并返回模块持有的私有状态。
2. `fixed_update` 在主线程执行零次或多次有界固定步长更新。
3. `update` 在主线程每帧至多执行一次。
4. `shutdown` 仅在初始化成功后调用，且最多一次；返回前销毁模块私有状态。

回调通过 `gneiss_result` 报告失败，异常不得穿过 C ABI。Game Context 和
`gneiss_game_update_time` 只在当前同步调用期间借用，模块不得持久化 Engine 内部对象或后端句柄。

## C++ 包装

`<gneiss/game_module.hpp>` 提供：

- `gneiss::game_context`：不拥有底层句柄的强类型包装。
- `gneiss::game_module_abi_version` 与 `gneiss::game_module_query_symbol`：编译期常量。
- `gneiss::validate_game_module`：返回 `gneiss::result` 的轻量描述校验。

C++ 包装不建立第二套模块状态，也不改变 C ABI 的所有权和线程规则。
