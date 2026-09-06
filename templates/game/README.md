<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Gneiss 游戏工程模板

该目录可复制为独立游戏工程。它只通过 `find_package(gneiss CONFIG REQUIRED)` 使用安装后的
`gneiss::engine`，并生成由 `gneiss_runtime` 加载的原生 Game Module。

先把 `GNEISS_SDK_ROOT` 设置为 Gneiss Shared SDK 安装前缀，然后执行：

```powershell
cmake --preset game-debug-configure
cmake --build --preset game-debug
$env:GNEISS_SDK_ROOT/bin/gneiss_runtime --project .
```

Linux 使用 `$GNEISS_SDK_ROOT/bin/gneiss_runtime --project .`。原生 Game Module 当前要求 Shared
Engine SDK；Static SDK 仍可供直接链接的应用使用，但不能提供这一动态模块闭环。

工程描述固定为根目录的 `gneiss.project.json`。新增资产放入 `assets/`，模块产物由 preset 写入
`modules/`，本地构建目录为 `build/`；这两个生成目录不应提交到版本控制。
