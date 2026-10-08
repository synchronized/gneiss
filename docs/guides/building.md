<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 构建与测试 Gneiss

## 适用场景

本指南用于配置、构建并验证当前 Gneiss 工程、version、属性检查、Granit 图形示例、Runtime 宿主和
Editor。

## 前置条件

- CMake 3.23 或更高版本。
- 支持 C++20 的 C/C++ 编译器。
- 使用 Ninja preset 时需要安装 Ninja。
- 启用 Granit 运行时适配时需要已安装的 Granit `0.44.0+` 核心、Window 与 RenderPipeline
  组件，或由父工程提供 `granit::granit`、`granit::window` 和
  `granit::render_pipeline` 目标。
- 离线工具或测试还需要 Granit `AssetTools` 组件。FETCH 自动启用其 SDK；PACKAGE 或父工程模式
  必须提供 `granit::asset_tools`。引擎 Runtime 不链接该工具组件。

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
子节点；未指定材质的 Primitive 使用生成的默认材质。支持 base color、metallic-roughness、normal、occlusion、emissive 五类 PNG 贴图。导入先写入
目标目录同级的暂存目录，全部成功后再替换目标目录，因此会清除上次导入遗留的文件；校验或写出
失败时保留原有完整结果。

把作者资产目录 Cook 为发布时使用的运行资产，并复用内容缓存：

```powershell
./build/windows-clang-debug/bin/gneiss_assetc.exe cook ./generated-assets `
  --output ./runtime-assets --cache ./build/asset-cache
./build/windows-clang-debug/bin/gneiss_assetc.exe inspect ./runtime-assets/textures/image-0.ktx2
```

`cook` 不覆盖已有输出目录。PNG 会转为带完整 Mip 的 RGBA8 KTX2，JSON URI 随之重写；相同源、
颜色空间、处理器版本和目标再次构建时命中缓存。`gneiss_project package` 与 Editor 发布对话框使用
同一个构建服务，通常不需要手动执行该命令。

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

下游项目使用 `find_package(gneiss CONFIG REQUIRED)` 和 `gneiss::engine`。Windows 共享库 Consumer
运行时需要让 `GNEISS_RUNTIME_DIR` 位于 `PATH`；静态库无需该运行时路径。引擎库本身不安装内置
资产；示例各自管理配套资产。启用 Granit 平台适配构建的安装包会继续要求同一安装环境提供 Granit
`Window`、`Input` 与 `RenderPipeline` package，但 Granit 类型不会进入 Gneiss 公共头文件。
如果 Granit 安装在另一个前缀，Windows 运行时还需把该前缀的 `bin` 加入 `PATH`；Gneiss 不会把
外部 Granit package 复制进自身安装前缀。
Linux Shared 构建应将 package 导出的 `GNEISS_LIBRARY_DIR` 及外部 Granit 前缀的 `lib` 加入动态库
搜索路径；仓库安装 Consumer 会自动配置该测试环境。

### 启用 Granit 窗口与渲染适配

普通 preset 默认关闭可选的运行时适配，因此无图形环境也能构建和测试核心。启用后，Granit 平台
Application 会创建 Vulkan Renderer、Surface 和 Swapchain，并在每帧更新后执行清屏与呈现。
Granit 0.43 的标准 PBR 要求设备支持 `shaderDemoteToHelperInvocation`；后端查询并启用该特性，
不支持时设备选择失败，不会继续执行依赖该能力的 Shader。
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

Gneiss 当前使用 Granit 原生 Window 后端，未接入 0.30 的可选 SDL3 Window 后端。
升级依赖不会自动切换到 SDL3；手动选择 SDL3 前仍需迁移 Gneiss 的原生句柄 Surface 创建路径。

使用 `GNEISS_GRANIT_PROVIDER=FETCH` 可以强制验证下载路径，跳过 package 查找。仓库镜像和版本可
通过 `GNEISS_GRANIT_GIT_REPOSITORY`、`GNEISS_GRANIT_GIT_TAG` 覆盖。若父工程已经定义
`granit::granit`、`granit::window` 与 `granit::render_pipeline`，所有 provider
都会优先直接复用。项目会自动更新仍沿用旧默认提交的构建目录，但不会改写其他自定义提交；若需
刻意固定旧默认提交，同时设置 `GNEISS_GRANIT_UPDATE_DEFAULTS=OFF`。Windows 使用共享库 package
时，构建会把 Granit 的运行时 DLL 自动复制到 Gneiss 的运行时输出目录，无需手动修改 `PATH`。

启用 Granit 适配并完成构建后，可以运行交互神殿或 Lantern 灯廊示例；按 `A`/`D` 绕场景旋转
观察视角，按 `Esc` 或关闭窗口正常退出：

```powershell
./build/granit-platform/bin/gneiss_temple_example.exe
./build/granit-platform/bin/gneiss_lantern_gallery_example.exe
```

1.0.0 的稳定运行时代表性样例覆盖同一图形路径，并提供独立的安装 SDK Consumer：

```powershell
./build/windows-clang-debug/bin/gneiss_stable_runtime_example.exe --smoke
./build/windows-clang-release/bin/gneiss_stable_runtime_example.exe --measure

cmake -S examples/stable_runtime -B build/stable-runtime-consumer `
  -DCMAKE_PREFIX_PATH=build/gneiss-install
cmake --build build/stable-runtime-consumer
ctest --test-dir build/stable-runtime-consumer --output-on-failure
```

独立 Consumer 需要同一前缀或 `CMAKE_PREFIX_PATH` 中同时提供启用 Window、Input 与 RenderPipeline
组件的 Granit package。配置时资产会复制到 Consumer 构建目录，运行不读取 Gneiss 源码树。
仓库的安装验收也会先把 Consumer 源码和资产复制到隔离目录，防止测试因源码树仍存在而误通过。
`--measure` 固定执行 60 帧预热和 300 帧采样，以单行 JSON 报告各启动阶段、退出阶段及稳定帧
的最小值、中位数、P95 和最大值。性能基线必须使用 Release 构建并重复采样；单次输出不能作为
回归阈值。`scripts/performance/measure_stable_runtime.py` 默认执行 1 次进程预热和 10 次有效采样，
保存原始数据、汇总、进程峰值常驻内存及环境元数据；使用 `--help` 查看必填的版本标识参数。
采样时必须显式填写 CPU、实际使用的 GPU 和驱动版本，不能仅凭系统枚举猜测 Vulkan 设备。

Linux Clang/GCC 可使用 `GNEISS_ENABLE_SANITIZERS=ON` 为 Gneiss 自有目标启用 AddressSanitizer、
LeakSanitizer 和 UndefinedBehaviorSanitizer。手动 Linux Actions 使用镜像已完整提供运行库的 GCC，
并在无头图形环境中运行
Application、场景故障矩阵和稳定运行时样例；Sanitizer 报告任何内存错误、未定义行为或退出泄漏时
任务失败。该选项不传播给安装后的下游项目，也不支持 Windows/MSVC。

Application 与场景故障测试启用严格 LSan。Mesa 软件 Vulkan ICD 会在进程退出时留下设备枚举缓存，
且卸载后间接分配只能显示为未知模块；为避免使用会掩盖自有问题的宽泛抑制，图形样例只运行
ASan/UBSan，并关闭 LSan。图形 Application 销毁时会先释放 Gneiss 持有的 Granit 子资源，再执行
后端无关的逻辑资源退出检查；仍有存活资源时，销毁返回 `invalid state`，诊断输出各资源类型数量。
Granit 的后端待回收数量只用于诊断，不视为调用方泄漏。

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

### 构建 Runtime 宿主

Runtime 宿主默认不参与普通构建，并需要 Granit 平台适配：

```sh
cmake --preset windows-clang-debug \
  -DGNEISS_ENABLE_GRANIT_PLATFORM=ON \
  -DGNEISS_BUILD_RUNTIME=ON
cmake --build --preset windows-clang-debug --target gneiss_runtime
```

从工程描述的 `startup_scene` 运行 Editor Demo，或固定运行三帧进行 smoke 验证：

```powershell
./build/windows-clang-debug/bin/gneiss_runtime.exe --project ./examples/editor_demo
./build/windows-clang-debug/bin/gneiss_runtime.exe --smoke --project ./examples/editor_demo
./build/windows-clang-debug/bin/gneiss_runtime.exe --project ./examples/editor_demo --log-file ./build/runtime.log
```

Runtime 宿主把 Application、诊断和 Game Module 事件以 `@gneiss-log-v1 ` 加单行 JSON 写入标准
输出，并把人类可读格式写入文件；创建 Application 前的启动日志和协议降级信息保持原始文本。
Windows 默认文件为
`%LOCALAPPDATA%/Gneiss/logs/runtime.log`；Linux 默认遵循 `$XDG_STATE_HOME`，未设置时使用
`$HOME/.local/state/gneiss/logs/runtime.log`。`--log-file <路径>` 可覆盖位置。日志达到 1 MiB 时旧文件
轮转为 `.1`；文件不可写只产生警告，不覆盖工程加载或运行的原始结果。

启用 Runtime 宿主后执行 `cmake --install` 会安装 `gneiss_runtime`、`gneiss_engine` 及所需 Granit
动态库。安装后的宿主仍以工程根为入口，不依赖源码树中的专用 `main` 函数。

### 构建 Editor

Editor 默认不参与普通构建。启用时会下载并静态构建固定提交的 Dear ImGui Docking 分支与 ImGuizmo
（均为 MIT），这些依赖只属于 `gneiss_editor`，不会传播到 Runtime 公共 ABI 或安装 package。
Editor 当前需要 Granit
平台适配。Project Manager 与正式 Editor 统一使用以 Catppuccin Mocha 为基础、Peach 为主强调色
的 `Gneiss Mocha` 主题，并使用 Inter Regular 与 Noto Sans SC 中文回退作为界面字体；上游配色许可见
`src/editor/CATPPUCCIN_NOTICE.md`，字体来源与许可见 `src/editor/fonts/README.md`：

```sh
cmake --preset windows-clang-debug \
  -DGNEISS_ENABLE_GRANIT_PLATFORM=ON \
  -DGNEISS_BUILD_EDITOR=ON \
  -DGNEISS_BUILD_RUNTIME=ON
cmake --build --preset windows-clang-debug --target gneiss_editor
```

同时构建 Runtime 后，Editor 的 `Run > Run Project`（F6）会在独立进程运行当前工程；F8 发出正常
停止请求。脏场景只提供“保存并运行”或“取消”，不会静默丢弃修改。`Console` 窗口逐条显示结构化
事件和 Raw 输出，可组合使用级别、当前会话、来源、分类与文本筛选，并支持暂停显示、清空、复制和
自动滚动。正常停止超过 2 秒后会强制终止并在 Console 中说明。该进程控制闭环当前已在 Windows
验证；Linux/POSIX 后端也已通过无头图形环境验收。

运行 Editor：

```powershell
./build/windows-clang-debug/bin/gneiss_editor.exe
```

不传参数时会先打开 Project Manager；可以直接输入工程目录，也可以在 Windows 使用系统目录选择器。
选择包含 `gneiss.project.json` 的目录并通过校验后，Project Manager 会完整关闭，再启动正式 Editor。
Project Manager 还会列出最近十个有效工程，并可从正式游戏模板创建包含初始 Camera、独立 Game
Module、CMake preset 和源码目录的工程。运行会依次完成工程配置、模块构建和 Runtime 启动。
也可以跳过选择界面，直接打开工程：

```powershell
./build/windows-clang-debug/bin/gneiss_editor.exe `
  --project ./examples/editor_demo
```

`--project` 只接收工程根目录，并保留为自动化与 smoke test 入口。Editor 固定读取该目录中的
`gneiss.project.json`；工程文件提供工程名称、资产根和初始场景，格式见
[工程文件格式 v1](../reference/project-format.md)。Editor 不再提供独立的
`--asset-root` 与 `--scene` 正式入口。

Lantern Gallery 的导入资产由构建过程生成，因此其可运行工程位于构建树，而不是源码树。完成
`windows-clang-debug` 构建后可以直接打开：

```powershell
./build/windows-clang-debug/bin/gneiss_editor.exe `
  --project ./build/windows-clang-debug/examples/lantern_gallery
```

安装时，完整工程位于 `${CMAKE_INSTALL_DATADIR}/gneiss/examples/lantern-gallery`。源码目录中的
`examples/lantern_gallery/gneiss.project.json` 是工程描述的权威来源，构建不会把 glTF 派生资产
写回源码树。

Editor Demo 会安装到 `${CMAKE_INSTALL_DATADIR}/gneiss/projects/editor-demo`，可用同一安装前缀中的
`bin/gneiss_runtime --project <工程目录>` 启动。`gneiss.runtime.installed-smoke` 会重建隔离安装前缀，
确认 Runtime、Engine、Granit 动态库和工程资产不依赖源码树路径。

### 从游戏工程模板开始

Shared SDK 会把可复制模板安装到 `${CMAKE_INSTALL_DATADIR}/gneiss/templates/game`。复制该目录，
设置 `GNEISS_SDK_ROOT` 为安装前缀，然后在新工程根执行：

```powershell
cmake --preset game-debug-configure
cmake --build --preset game-debug
$env:GNEISS_SDK_ROOT/bin/gneiss_runtime --project .
```

模板仅通过 `find_package(gneiss CONFIG REQUIRED)` 和 `gneiss::engine` 消费安装 SDK，并包含最小场景、
工程描述及 Game Module 生命周期。原生 Game Module 当前要求 Shared Engine SDK；使用 Static SDK
配置模板会被明确拒绝。CMake package 通过 `GNEISS_GAME_TEMPLATE_DIR` 暴露模板的绝对安装位置；
完整字段见[工程文件格式](../reference/project-format.md)。

安装了 Editor 的 Shared SDK 也提供 `gneiss_project`，用于创建具有独立模块 ID 的工程，并按指定
配置完成构建和发布包生成：

```powershell
$env:GNEISS_SDK_ROOT/bin/gneiss_project create ./MyGame "My Game"
$env:GNEISS_SDK_ROOT/bin/gneiss_project package ./MyGame `
  $env:GNEISS_SDK_ROOT/bin/gneiss_runtime ./MyGamePackage development --zip
$env:GNEISS_SDK_ROOT/bin/gneiss_project verify ./MyGamePackage
```

Windows 从 `MyGamePackage/run.cmd` 启动，Linux 从 `MyGamePackage/run.sh` 启动。可选配置为 `debug`、
`development` 和 `shipping`，默认使用 Development。目录包只包含工程描述、所选模块、Runtime、
动态依赖及资产，不包含 `sources/`、CMake 文件、构建目录或 SDK；`--zip` 同时生成确定性归档。

当前宿主已提供场景会话、可选择的层级树和独立 Editor Camera。鼠标位于 Scene View 时，可以使用
`W/A/S/D` 前后左右移动、`Q/E` 降低或升高、按住鼠标右键环视、滚轮沿视线移动；选择层级节点后
按 `F` 可聚焦其世界位置。Scene Hierarchy 可以创建、重命名、拖放重挂接、复制和删除节点；右键
菜单提供常用操作，也可使用 `F2`、`Ctrl+D` 和 `Delete`。Scene View 会以黄色边框和名称反馈当前
选择，Inspector 可以添加或移除 Camera 和 Mesh Renderer，并展示实体上已注册的属性。Inspector
根据 Type Registry 元数据生成布尔、
标量、向量和四元数控件；只读字段会禁用，非法值会保留运行时原值并显示错误。输入、字体 Texture
RID、UI Draw List 与 Granit Canvas 已完成同帧渲染。成功修改后状态显示为 `Modified`；点击
`Save` 或按 `Ctrl+S` 会原子写回当前场景。File 菜单还提供 New Scene、Open Scene 和 Save As，
对应快捷键为 `Ctrl+N`、`Ctrl+O` 和 `Ctrl+Shift+S`；路径必须位于工程资产根。切换场景、菜单退出
或关闭窗口时，未保存修改统一显示 Save/Discard/Cancel。保存成功后恢复为 `Saved`，失败时保留
源文件与脏状态并显示错误。Editor 主窗口支持系统缩放，ImGui 使用当前客户区尺寸；Scene View、
Hierarchy、Inspector、Asset Browser 和 Console 可以拖动标题栏与边界调整位置和大小。Editor 已
启用单窗口 Docking，可手动吸附、分栏和组成标签页。首次启动会建立左侧 Hierarchy/Assets、中央
Scene View、右侧 Inspector 和底部 Console 的确定性默认工作区。普通启动会在用户配置根下按工程
隔离保存布局，`Window > Reset Layout` 可恢复默认工作区；损坏或不兼容的布局不会阻止启动。可用
`Window` 菜单关闭或重新打开 Hierarchy、Assets、Scene View、Inspector 和 Console。可用 `--smoke`
固定运行 3 帧，验证场景加载、UI 提交与逆序清理，且不会读写开发机布局状态。关闭的主面板会随
工程布局一并恢复；旧版 v1 布局会迁移并默认显示全部面板。

## 验证结果

测试应报告公共 C/C++ 接口、内部行为、固定帧数 Granit smoke test 和 version 示例通过。version
示例输出当前项目版本：

```text
gneiss 0.50.0
```

开发 preset 默认启用编译警告并将警告视为错误。

### 手动验证矩阵

仓库不在推送、Pull Request 或合并时自动运行 Actions。需要远端验证时，在 GitHub Actions 页面手动
触发 Linux 和 Windows 工作流；工作流使用触发时选择的分支或提交，并执行以下矩阵：

- Windows Server 2022：MSVC、共享/静态安装 Consumer 与 Granit 运行时；托管 Runner 缺少 Vulkan
  ICD，因此窗口 smoke test 由 Linux 执行。
- Ubuntu 24.04：Clang/GCC、共享/静态核心与安装 Consumer；Clang 额外执行共享/静态 Granit 无头
  窗口测试，并构建 Editor/Runtime，验证跨进程场景切换、取消和重试。
- Ubuntu 24.04 独立任务：固定 Emscripten 5.0.6，构建无线程最小调度宿主并在真实 Chrome 中执行；
  上传浏览器 DOM 和诊断日志。

工作流配置位于 `.github/workflows/windows.yml` 和 `.github/workflows/linux.yml`。工作流是否通过以
对应手动运行的 Actions 结果为准。同一候选提交的完整矩阵原则上只运行一次；仅当代码、构建、依赖
或测试发生可能影响结果的变化时，才重跑受影响的工作流，纯文档验收更新不重复触发。

当用户要求“推进版本直到完成”时，版本完成包含：本地验收、推送特性分支、创建或更新单一 Pull
Request、手动远端验证、修复阻塞问题、合并到 `main`、创建并推送版本标签，以及按仓库既定方式
创建发布。该表述视为对当前版本完整闭环的一次授权，不在每个远端步骤前重复确认；授权不会延续到
下一版本，也不会扩展到 Granit 等其他仓库。

## 最小无线程浏览器调度验证

此入口只构建内部任务调度器，不构建完整 Editor、Runtime、Granit 或 libuv。
本机验证工具链为 Emscripten 5.0.6；保留 C++ 异常处理，显式禁用工作线程后端。

```sh
emcmake cmake -S tests/task_web -B build/task-web -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/task-web
python tests/task_web/run_browser.py --build build/task-web --browser <Chrome或Chromium可执行文件>
```

Windows 的 Emscripten PowerShell 入口需要 `EMSDK_PYTHON` 指向 SDK 的 Python，并按 SDK 配置
设置 `EM_CONFIG`。测试驱动启动本地临时 HTTP 服务，在真实无头浏览器中检查跨事件循环延迟执行、
异常回执、取消及非阻塞关闭；结果保存在构建目录的 `browser-dom.html` 和 `browser.log`。
本入口没有 `-pthread`、SharedArrayBuffer 或 COOP/COEP 前提，不代表完整引擎 Web 支持。

Editor 的 `--cooperative-tasks` 选项让资产服务由帧循环显式驱动，默认仍使用工作线程池。
每次驱动最多执行 8 个任务、在任务之间检查 2 ms 时间预算；单个导入或解码不可被预算抢占。

## 真实大场景测量

Sponza 是显式下载的外部测试资产，不随仓库和源码 Release 分发。先按
[固定资产审计与基线记录](../records/M-272-sponza-baseline.md) 获取、校验并生成日常/完整配置，
再构建 `gneiss_scene_load_baseline`。`scripts/performance/measure_scene_loading.py --help` 给出
Release 路径、可选 Debug 功能矩阵与输出目录参数；工具顺序运行两种模式，每配置三次，保存 JSON、呈现观测 CSV
和 GPU 回读 PPM。输出目录必须不存在，资产缺失直接报错，不把缺少外部数据记为通过。
性能门槛只检查 Release，两种执行模式均检查响应性。`--daily-assets`、`--full-assets`
可指定现有 Cook 目录；默认资源数为 505，旧夹具需显式使用 `--expected-resources 458`。
`--baseline docs/records/artifacts/0.49-baseline-release.json` 还会比较加载中位数、进程峰值与图像。
资产身份和门槛见 [0.49 基线记录](../records/M-312-loading-stability-baseline.md)。

诊断后台加载的长间隔时，可单独运行测量程序并追加 `trace`：

```powershell
./build/windows-clang-release/bin/gneiss_scene_load_baseline.exe <资产目录> <输出前缀> thread initial trace
```

输出前缀的父目录须存在。常规 JSON 额外记录样本数、P50/P99 和超过
16.7/33.3/50/100 ms 的次数及比例；旧 P95/最大值定义保持。
可选 `.loop.csv` 保存每次更新回调的起点、此前已观测场景阶段、回调总耗时、
协作调度、场景推进及实际睡眠耗时。后三项嵌套于回调总耗时，不能相加后再加总耗时。
`outside_callback_ms` 是直到下一回调的剩余时间，含渲染、下一轮事件处理和观测开销，
尚不能归因于某个引擎调用；最后一行未闭合，使用 -1。墙钟耗时包含线程失去调度的时间。
详细记录在测量完成后批量写出，默认关闭；开启时会增加内存与计时开销，须另作开关对照。
宿主原有 `sleep_for(1 ms)` 保持，不能将移除等待导致的数字下降宣称为引擎优化。

要同时观测引擎内部，可在单个进程启动前设置 `GNEISS_LOOP_TRACE` 为可写的 CSV 路径，
父目录须存在；诊断同样适用于实际 `gneiss_runtime --smoke --project <工程目录>`。

```powershell
$gneissPreviousTrace = $env:GNEISS_LOOP_TRACE
try {
  $env:GNEISS_LOOP_TRACE = Join-Path (Get-Location) 'build/runtime-loop.csv'
  ./build/windows-clang-release/bin/gneiss_runtime.exe --smoke --project <工程目录>
} finally {
  $env:GNEISS_LOOP_TRACE = $gneissPreviousTrace
}
```

每次 Application::run 结束后追加一个 CSV 表，开头记录单调时钟 origin_ns，
最多保留最长 128 个循环及一行 maxima；maxima 的 frame 列表示该轮 run 的总循环数，
各阶段最大值独立统计，不能相加。末尾注释统计循环累计时间及阈值超限次数。
多个 run 的预热、加载和回读不能混作同一组；诊断路径应为本次进程单独指定。

`total_ms` 为本次循环体加前一循环结束后的间隔，`gap_ms` 单列循环簿记与调度间隔。
它与测量宿主旧 event_interval 的边界不同。事件、更新、渲染与绘制列表清理为顶层阶段；
场景推进、资产接收/发布、上传回执/提交、候选收尾与帧快照等为嵌套阶段。
window_pump/window_events 区分平台事件泵与窗口队列接收，input_state/input_events 区分
输入状态读取与事件转换；task_collect/task_submit 记录加载服务的任务结果接收与续步提交。
task_submit_lock 与 task_submit_work 区分提交取锁和锁内工作；allocate/insert/notify 子项
嵌套于 work。idle_lock 是等待前初次取锁，idle_condition 包含条件等待及其内部重新取锁。
这些子项仍包含线程失去调度的时间，不把它们直接解释成 CPU 执行，也不重复加总。
清理计时不等同于 GPU 驱动资源已经释放；run 返回前的 finish_frames 等待及关闭不在循环体内。
最长样本不能用于推算全量 P95。详细计时默认关闭，关闭时不读取时钟或分配每帧诊断记录；
开启时保留固定容量数据，文件写入在循环结束后完成。应对照插桩开销，不把诊断耗时计作产品优化。
输出失败向 stderr 报告，不改变 Application 的运行结果。

启用工作线程的原生 Granit Application 在循环末尾使用进展通知和最多请求 4 ms 的有限等待，
避免无进展时持续轮询。任务终态及渲染回执可提前结束等待；未接入原生窗口等待接口时，
输入与 IPC 依靠截止期限重新轮询。实际唤醒可能受 OS 调度推迟，不能把 4 ms 当作实测上限。
诊断新增 idle_wait_ms，属于完整循环时间的一部分；Headless、Web 和无线程配置不启用该等待。
资产工作线程在异步区间读取未完成时，每次推进可在原生 I/O 完成事件上最多请求 4 ms 等待；
协作模式不使用该等待，read_operation.poll 仍保持非阻塞。Runtime 的 task_statistics 日志记录
主循环结束时的任务提交、完成与保留数，可用于区分有意义的工作和频繁续步。
Windows 的 gneiss_main_loop_response 测试通过独立线程发送按键与任务；可指定 JSON 输出路径，
再加参数 stall 注入一次 50 ms 主线程停顿，用于验证测量夹具自身。可再加 `--assets <Cook资产根>`，在完整场景加载期间持续投递直到激活，并记录节点与资源数；
它使用隐藏 320×240 窗口和单工作线程，不将耗时与 Runtime 吞吐对照混比，也不代替像素或窗口恢复验收。
指定输出路径并加 `--trace` 可额外生成 `<输出路径>.probes.csv`，分别保留最慢的 16 个输入和任务探针。
序号用于关联两种接收，绝对 steady_clock 纳秒时间戳可与 `GNEISS_LOOP_TRACE` 的 `origin_ns`
对齐；输入起点在提交任务及 PostMessage 之前，任务起点在工作函数结束、终态发布之前。
此诊断默认关闭，不改变响应门槛；未复现的超限样本不能被诊断通过覆盖。

用同一进程的两份 CSV 离线关联探针与保留的循环：

```powershell
python -X utf8 -B scripts/performance/correlate_loop_response.py `
  <引擎循环.csv> <输出.json.probes.csv> --output <关联报告.json>
```

输出区分循环前空档、循环内部重叠与未保留时间。`total_ms` 包含 `gap_ms`，
循环内部的结束时刻为 `start_ms + total_ms - gap_ms`。阶段耗时是整轮累计且可能嵌套，
不能直接相加或视作探针窗口内的耗时；关联报告不代替原始门槛验收。
场景收尾额外记录 `scene_verify`、`scene_builder_create`、`scene_asset_service_reset`
与 `scene_build`，分别覆盖复验推进、构建器创建、资产服务切换和节点分批构建。
其中构建器创建与资产服务切换包含在复验推进内，不重复累加。


Windows 下可使用下列工具顺序测量实际 Runtime 的进程/主线程 CPU 时间，并交替运行诊断开关：

```powershell
python -X utf8 -B scripts/performance/measure_runtime_loop.py `
  --runtime build/windows-clang-release/bin/gneiss_runtime.exe `
  --daily-project <日常工程目录> --full-project <完整工程目录> `
  --output build/runtime-cpu-baseline --repeat 3
```

0.50 候选采样完成后，用冻结基线检查 CPU、吞吐、工作集和循环最大间隔；输出文件须尚不存在：

```powershell
python -X utf8 -B scripts/performance/validate_runtime_loop.py `
  docs/records/artifacts/0.50-runtime-cpu-baseline.json `
  build/runtime-cpu-baseline/summary.json --output build/runtime-cpu-gates.json
```

该检查不代替资产身份、窗口输入、生命周期与图像验收；缺少三次独立样本或报告未完成即失败。

两个工程须已准备好 gneiss.project.json 与工程内资产；输出目录必须尚不存在。
工具不生成或修改资产，不清理系统缓存，每组启动独立进程；测试期间不要并行构建或运行其他 GPU 测试。
每次保存日志、退出状态和可选循环 CSV，summary.json 持续保存已完成样本，失败不丢弃此前结果。
成功须同时有场景激活和正常退出日志。默认每次期限 900 秒，超时只结束工具自己启动的子进程。

CPU 数据包含启动与退出；主线程取进程中最早创建的线程，并保留句柄读取最终 user + kernel 时间。
main_thread_core_equivalent 为主线程 CPU 时间 / 进程墙钟时间，1 表示约一个核心，
不是整机 CPU 百分比。退出通过 250 ms 轮询发现，墙钟时间可能包含末尾轮询延迟与测量开销，
不能用微小开关差异推断精确插桩成本。内存每 250 ms 查询历史工作集峰值，可能漏掉末次采样后的峰值；
它不是 GPU 显存，也不是资产候选预算。Runtime smoke 没有测量交互输入延迟或捕获验收图像。

普通 CI 使用原创小夹具进行 GPU 像素、候选原子性与 IPC 生命周期回归，不下载数 GiB 的 Sponza。
大场景结果与测量边界见 [0.42 验收记录](../records/M-273-278-0.42.0-validation.md)。

## 常见问题

- 找不到编译器：确认 preset 指定的编译器已加入 `PATH`，或选择其他 preset。
- 找不到 Ninja：安装 Ninja，或在 Windows 上选择 Visual Studio 2022 preset。
- 切换编译器或链接方式：使用对应的独立 preset，不要复用其他 preset 的构建目录。
- Windows MSBuild 路径限制：FETCH 将 Granit 放在较短的 `_deps/granit` 构建目录。若仓库路径仍
  过长并出现 MSB3491，可用 `cmake --preset <preset> -B <较短目录>` 配置，再对该目录执行
  `cmake --build <目录> --config Release` 和 `ctest --test-dir <目录> -C Release`。
- 找不到 Granit：确认安装前缀包含 `lib/cmake/granit/granitConfig.cmake`，且安装时包含 Window
  组件；源码联调时由父工程先添加 Granit，再添加 Gneiss；无网络环境使用 `PACKAGE`，避免 AUTO
  在 package 缺失时尝试下载。


手动 Windows/Linux Actions 的 `scope` 默认为 `all`，运行完整发布矩阵。只修改 Editor/Runtime
或其测试后可选 `editor` 补验宿主矩阵；该范围不能代替首次完整验收，未执行的核心、Web 和安装
任务仍须单独完成。纯记录更新不重跑矩阵。
