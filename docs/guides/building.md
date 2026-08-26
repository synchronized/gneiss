<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 构建与测试 Gneiss

## 适用场景

本指南用于配置、构建并验证当前 Gneiss 最小工程和 version 示例。

## 前置条件

- CMake 3.23 或更高版本。
- 支持 C++20 的 C/C++ 编译器。
- 使用 Ninja preset 时需要安装 Ninja。
- 启用窗口适配时需要已安装的 Granit `0.3.0+` Window 组件，或由父工程提供
  `granit::window` 目标。

## 操作步骤

先查看当前平台可用的 preset：

```sh
cmake --list-presets
```

选择一个 preset 进行配置、构建和测试，例如 Windows Clang Debug：

```sh
cmake --preset windows-clang-debug
cmake --build --preset windows-clang-debug
ctest --preset windows-clang-debug
```

运行 version 示例：

```powershell
./build/windows-clang-debug/bin/gneiss_version_example.exe
```

Linux 可选择 `linux-clang-debug` 或 `linux-gcc-debug`，可执行文件不带 `.exe` 后缀。

### 启用 Granit 窗口适配

普通 preset 默认关闭可选的窗口适配，因此无图形环境也能构建和测试核心。使用已安装的 Granit
package 时，在独立构建目录中显式启用：

```sh
cmake -S . -B build/granit-platform -G Ninja \
  -DGNEISS_ENABLE_GRANIT_PLATFORM=ON \
  -DCMAKE_PREFIX_PATH=/path/to/granit/install
cmake --build build/granit-platform
ctest --test-dir build/granit-platform --output-on-failure
```

若父工程已经定义 `granit::window`，Gneiss 会直接复用，不再执行 package 查找。共享库构建运行
测试时，Granit Window 动态库必须位于系统动态库搜索路径中。

## 验证结果

测试应报告公共 C/C++ 接口、内部行为和 version 示例通过。示例输出当前项目版本：

```text
gneiss 0.1.0
```

开发 preset 默认启用编译警告并将警告视为错误。

## 常见问题

- 找不到编译器：确认 preset 指定的编译器已加入 `PATH`，或选择其他 preset。
- 找不到 Ninja：安装 Ninja，或在 Windows 上选择 Visual Studio 2022 preset。
- 切换编译器或链接方式：使用对应的独立 preset，不要复用其他 preset 的构建目录。
- 找不到 Granit：确认安装前缀包含 `lib/cmake/granit/granitConfig.cmake`，且安装时包含 Window
  组件；源码联调时由父工程先添加 Granit，再添加 Gneiss。
