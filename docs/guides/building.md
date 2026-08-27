<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 构建与测试 Gneiss

## 适用场景

本指南用于配置、构建并验证当前 Gneiss 工程、version 示例和 Granit 三角形示例。

## 前置条件

- CMake 3.23 或更高版本。
- 支持 C++20 的 C/C++ 编译器。
- 使用 Ninja preset 时需要安装 Ninja。
- 启用 Granit 运行时适配时需要已安装的 Granit `0.3.0+` 核心、Window 与 Input 组件，或由父工程
  提供 `granit::granit`、`granit::window` 和 `granit::input` 目标。

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

### 安装并通过 CMake package 使用

构建后可将库、公共头文件、CMake package 和示例资产安装到同一前缀：

```sh
cmake --install build/windows-clang-debug --prefix build/gneiss-install
```

下游项目使用 `find_package(gneiss CONFIG REQUIRED)` 和 `gneiss::gneiss`。配置文件同时提供
`GNEISS_ASSET_DIR`，指向可重定位的安装资产目录。Windows 共享库 Consumer 运行时需要让
`GNEISS_RUNTIME_DIR` 位于 `PATH`；静态库无需该运行时路径。启用 Granit 平台适配构建的安装包会
继续要求同一安装环境提供 Granit `Window` 与 `Input` package，但 Granit 类型不会进入 Gneiss
公共头文件。

### 启用 Granit 窗口与渲染适配

普通 preset 默认关闭可选的运行时适配，因此无图形环境也能构建和测试核心。启用后，Granit 平台
Application 会创建 Vulkan Renderer、Surface 和 Swapchain，并在每帧更新后执行清屏与呈现。
依赖解析默认使用 `AUTO`
provider：优先复用父工程目标，其次查找 package，最后把锁定的 Granit 提交下载到当前构建目录的
`_deps`。开箱构建命令如下：

```sh
cmake -S . -B build/granit-platform -G Ninja \
  -DGNEISS_ENABLE_GRANIT_PLATFORM=ON
cmake --build build/granit-platform
ctest --test-dir build/granit-platform --output-on-failure
```

发行、离线或严格 CI 应只允许已安装 package：

```sh
cmake -S . -B build/granit-platform -G Ninja \
  -DGNEISS_ENABLE_GRANIT_PLATFORM=ON \
  -DGNEISS_GRANIT_PROVIDER=PACKAGE \
  -DCMAKE_PREFIX_PATH=/path/to/granit/install
cmake --build build/granit-platform
ctest --test-dir build/granit-platform --output-on-failure
```

使用 `GNEISS_GRANIT_PROVIDER=FETCH` 可以强制验证下载路径，跳过 package 查找。仓库镜像和版本可
通过 `GNEISS_GRANIT_GIT_REPOSITORY`、`GNEISS_GRANIT_GIT_TAG` 覆盖。若父工程已经定义
`granit::granit` 与 `granit::window`，所有 provider 都会优先直接复用。Windows 使用共享库 package
时，构建会把 Granit 的运行时 DLL 自动复制到 Gneiss 的运行时输出目录，无需手动修改 `PATH`。

启用 Granit 适配并完成构建后，可以运行交互三角形示例；按 `A`/`D` 反向旋转，按 `Esc` 或关闭
窗口正常退出：

```powershell
./build/granit-platform/bin/gneiss_triangle_example.exe
```

Linux 下运行同名且不带 `.exe` 后缀的可执行文件。该示例的 `main` 位于
`examples/triangle/main.cpp`，只使用 Gneiss 公共接口创建 Application、加载场景实例并按对象 UUID
更新 Scene Node，并通过动作映射消费输入。运行命令需要从仓库根目录执行，使默认资产根 `assets`
可见；示例的 Mesh、Material、Camera 和对象结构均来自 `assets/scenes/triangle.scene.json`。

## 验证结果

测试应报告公共 C/C++ 接口、内部行为、固定帧数 Granit smoke test 和 version 示例通过。version
示例输出当前项目版本：

```text
gneiss 0.2.0
```

开发 preset 默认启用编译警告并将警告视为错误。

### 手动验证矩阵

仓库不在推送、Pull Request 或合并时自动运行 Actions。需要远端验证时，在 GitHub Actions 页面手动
触发 Linux 和 Windows 工作流；工作流使用触发时选择的分支或提交，并执行以下矩阵：

- Windows Server 2022：MSVC、共享/静态安装 Consumer 与 Granit 运行时；托管 Runner 缺少 Vulkan
  ICD，因此窗口 smoke test 由 Linux 执行。
- Ubuntu 24.04：Clang/GCC、共享/静态核心与安装 Consumer；Clang 额外执行共享/静态 Granit 无头
  窗口测试。

工作流配置位于 `.github/workflows/windows.yml` 和 `.github/workflows/linux.yml`。是否触发以及验证
哪个提交属于显式发布或评审步骤；工作流是否通过以对应手动运行的 Actions 结果为准。

## 常见问题

- 找不到编译器：确认 preset 指定的编译器已加入 `PATH`，或选择其他 preset。
- 找不到 Ninja：安装 Ninja，或在 Windows 上选择 Visual Studio 2022 preset。
- 切换编译器或链接方式：使用对应的独立 preset，不要复用其他 preset 的构建目录。
- 找不到 Granit：确认安装前缀包含 `lib/cmake/granit/granitConfig.cmake`，且安装时包含 Window
  组件；源码联调时由父工程先添加 Granit，再添加 Gneiss；无网络环境使用 `PACKAGE`，避免 AUTO
  在 package 缺失时尝试下载。
