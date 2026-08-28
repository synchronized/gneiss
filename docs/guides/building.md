<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 构建与测试 Gneiss

## 适用场景

本指南用于配置、构建并验证当前 Gneiss 工程、version、属性检查、Granit 图形示例和 Editor。

## 前置条件

- CMake 3.23 或更高版本。
- 支持 C++20 的 C/C++ 编译器。
- 使用 Ninja preset 时需要安装 Ninja。
- 启用 Granit 运行时适配时需要已安装的 Granit `0.4.0+` 核心、Window、Input 与 RenderPipeline
  组件，或由父工程提供 `granit::granit`、`granit::window`、`granit::input` 和
  `granit::render_pipeline` 目标。

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

运行无 UI 的属性检查示例：

```powershell
./build/windows-clang-debug/bin/gneiss_property_inspector_example.exe
```

该示例只使用公共 C11/C++20 SDK：枚举 Transform 与 Camera 元数据，通过稳定 Type ID 和 Field ID
修改场景实体属性，将当前场景序列化到临时目录，再创建新的 Application 重新加载并校验。它不依赖
EnTT、Granit 类型或私有组件头，运行完成后会清理自己创建的临时目录。

Linux 可选择 `linux-clang-debug` 或 `linux-gcc-debug`，可执行文件不带 `.exe` 后缀。

### 构建资产工具

顶层构建默认启用离线工具，也可通过 `GNEISS_BUILD_TOOLS=ON/OFF` 显式控制。启用后会在构建目录
下载并静态构建锁定的 fastgltf 与 simdjson，不会把二者传播给 Runtime 或安装 package。当前可用
的首个检查命令为：

```powershell
./build/windows-clang-debug/bin/gneiss_assetc.exe inspect ./tests/data/gltf/static_triangle.gltf
```

该命令验证静态 glTF 的基础能力边界并输出场景摘要，不写入资产。

将受支持的静态 glTF 转换为 Runtime 资产目录：

```powershell
./build/windows-clang-debug/bin/gneiss_assetc.exe import ./model.gltf --output ./generated-assets
```

当前命令确定性生成 `models`、`materials`、`textures` 和 `scenes` 子目录。入口场景固定为
`scenes/scene.scene.json`。包含多个 Primitive 的 Mesh 会拆分为独立 Mesh 资产和稳定的合成场景
子节点；未指定材质的 Primitive 使用生成的默认材质。首版纹理仅支持基础颜色 PNG。导入先写入
目标目录同级的暂存目录，全部成功后再替换目标目录，因此会清除上次导入遗留的文件；校验或写出
失败时保留原有完整结果。

导入生成的 Mesh 使用 `.gneiss-mesh` 二进制格式。可按需检查、严格验证或导出 Debug JSON：

```powershell
./build/windows-clang-debug/bin/gneiss_assetc.exe inspect ./generated-assets/models/mesh-0-primitive-0.gneiss-mesh
./build/windows-clang-debug/bin/gneiss_assetc.exe validate ./generated-assets/models/mesh-0-primitive-0.gneiss-mesh
./build/windows-clang-debug/bin/gneiss_assetc.exe dump ./generated-assets/models/mesh-0-primitive-0.gneiss-mesh --format json
```

### 安装并通过 CMake package 使用

构建后可将库、公共头文件、CMake package 和示例资产安装到同一前缀：

```sh
cmake --install build/windows-clang-debug --prefix build/gneiss-install
```

下游项目使用 `find_package(gneiss CONFIG REQUIRED)` 和 `gneiss::gneiss`。Windows 共享库 Consumer
运行时需要让 `GNEISS_RUNTIME_DIR` 位于 `PATH`；静态库无需该运行时路径。引擎库本身不安装内置
资产；示例各自管理配套资产。启用 Granit 平台适配构建的安装包会继续要求同一安装环境提供 Granit
`Window`、`Input` 与 `RenderPipeline` package，但 Granit 类型不会进入 Gneiss 公共头文件。

### 启用 Granit 窗口与渲染适配

普通 preset 默认关闭可选的运行时适配，因此无图形环境也能构建和测试核心。启用后，Granit 平台
Application 会创建 Vulkan Renderer、Surface 和 Swapchain，并在每帧更新后执行清屏与呈现。
Renderer 初始化时会查询设备的 Uniform Buffer 对齐与绑定范围；渲染服务按设备对齐创建逐帧
Uniform Arena，并通过动态 Offset 为同一帧的不同对象提供变换与材质颜色。静态 Mesh 首次使用时
会打包到持久 GPU 几何 Arena，多个对象实例不再逐帧重复上传相同 Vertex/Index 数据。
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
`granit::granit`、`granit::window`、`granit::input` 与 `granit::render_pipeline`，所有 provider
都会优先直接复用。Windows 使用共享库 package
时，构建会把 Granit 的运行时 DLL 自动复制到 Gneiss 的运行时输出目录，无需手动修改 `PATH`。

启用 Granit 适配并完成构建后，可以运行交互神殿或 Lantern 灯廊示例；按 `A`/`D` 绕场景旋转
观察视角，按 `Esc` 或关闭窗口正常退出：

```powershell
./build/granit-platform/bin/gneiss_temple_example.exe
./build/granit-platform/bin/gneiss_lantern_gallery_example.exe
```

Linux 下运行同名且不带 `.exe` 后缀的可执行文件。该示例的 `main` 位于
`examples/temple/main.cpp`，只使用 Gneiss 公共接口创建 Application、加载场景实例并按对象 UUID
更新 Camera Scene Node，并通过动作映射消费输入。示例资产完整位于
`examples/temple/assets`，因此从任意工作目录启动构建产物都能运行；安装后的可执行文件会从
`share/gneiss/examples/temple/assets` 定位配套资产。场景入口是
`examples/temple/assets/scenes/temple.scene.json`。

Lantern 灯廊示例在构建时使用 `gneiss_assetc` 把 CC0 `Lantern.glb` 导入构建目录，再叠加项目原创
的地面、石柱、相机和场景描述。源码只保留原始 GLB、上游许可和原创资产，避免同时维护生成文件。
来源与校验值见 `examples/lantern_gallery/assets/ASSET_ORIGINS.md`；该示例因此要求
`GNEISS_BUILD_TOOLS=ON`。使用 `--smoke --profile` 可以固定运行 3 帧，并输出 Application、Scene
与资产、输入和运行阶段的耗时；示例使用 512×512 派生基础色纹理控制启动成本。

### 构建 Editor

Editor 默认不参与普通构建。启用时会下载并静态构建固定提交的 Dear ImGui v1.92.9b（MIT），该
依赖只属于 `gneiss_editor`，不会传播到 Runtime 公共 ABI 或安装 package。Editor 当前需要 Granit
平台适配：

```sh
cmake --preset windows-clang-debug \
  -DGNEISS_ENABLE_GRANIT_PLATFORM=ON \
  -DGNEISS_BUILD_EDITOR=ON
cmake --build --preset windows-clang-debug --target gneiss_editor
```

运行 Editor：

```powershell
./build/windows-clang-debug/bin/gneiss_editor.exe
```

不传参数时会先打开 Project Manager；可以直接输入工程目录，也可以在 Windows 使用系统目录选择器。
选择包含 `gneiss.project.json` 的目录并通过校验后，Project Manager 会完整关闭，再启动正式 Editor。
也可以跳过选择界面，直接打开工程：

```powershell
./build/windows-clang-debug/bin/gneiss_editor.exe `
  --project ./examples/editor_demo
```

`--project` 可接收工程目录或目录中的 `gneiss.project.json`，并保留为自动化与 smoke test 入口。
工程文件提供工程名称、资产根和初始场景，格式见[工程文件格式 v1](../reference/project-format.md)。
Editor 不再提供独立的
`--asset-root` 与 `--scene` 正式入口。

当前宿主已提供场景会话、可选择的层级树和独立 Editor Camera。鼠标位于 Scene View 时，可以使用
`W/A/S/D` 前后左右移动、`Q/E` 降低或升高、按住鼠标右键环视、滚轮沿视线移动；选择层级节点后
按 `F` 可聚焦其世界位置。Scene View 会以黄色边框和名称反馈当前选择，Inspector 展示节点
标识以及实体上已注册的 Transform、Camera 属性。Inspector 根据 Type Registry 元数据生成布尔、
标量、向量和四元数控件；只读字段会禁用，非法值会保留运行时原值并显示错误。输入、字体 Texture
RID、UI Draw List 与 Granit Canvas 已完成同帧渲染。成功修改后状态显示为 `Modified`；点击
`Save` 或按 `Ctrl+S` 会原子写回启动参数指定资产根中的源场景，成功后恢复为 `Saved`，失败时保留
源文件与脏状态并显示错误。窗口暂时固定为 1280×720。可用 `--smoke` 固定运行 3 帧，验证场景
加载、UI 提交与逆序清理。

## 验证结果

测试应报告公共 C/C++ 接口、内部行为、固定帧数 Granit smoke test 和 version 示例通过。version
示例输出当前项目版本：

```text
gneiss 0.5.0
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
