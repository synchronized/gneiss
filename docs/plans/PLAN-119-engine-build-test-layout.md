<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-119：Engine 头文件、构建目标与测试组织整理

## 状态与范围

版本级剩余项统一见 [DEV-052](DEV-052-architecture-cleanup.md#收尾审计后的明确待办)，本计划保留实施步骤与历史记录。

2026-11-18：目录、CMake 目标与测试组织实施及本地 Windows 验收已收尾。

- P2：Engine/Runtime/Editor/Tooling 契约与实现目录已迁移，旧头复制清单退出。
- P3：目标归属、共享/静态真实消费者、指定前缀安装及安装消费者已核查；冗余白盒源码已删除，
  保留白盒边界的理由见[测试分类与构建验收](../records/2026-11-18-test-classification-build-acceptance.md)。
- P4：测试按领域组织，目标统一 Tests 分组，233 项分类为 213 CPU、20 GPU；辅助函数清理见
  [测试调用审查](../records/2026-11-18-test-api-review.md)。后续随接口变化维护，不重复全目录重写。
- P5：当前 Guide/Reference 的旧目录、core-boundary 和 public-api 扫描范围已更新并验证。
- 本轮 MSVC Release 编译诊断、DLL 导出修正、完整构建和测试证据见
  [GPU 与 MSVC 验收](../records/2026-11-18-gpu-msvc-build-acceptance.md)。

本地整理闭环不代表跨平台或版本发布完成。Linux/Web、GPU 专门压力与预算验收仍由 DEV-052
A52-04/A52-05 管理；可以恢复 PLAN-118 P2 的 Meta 属性读取链审计，不再重复目录迁移。
目标：明确 Engine 与其余仓内模块的目录和契约边界，消除 CMake 旧路径和重复维护，
使目标依赖与代码所有权一致，降低测试对实现布局的耦合。
非目标：重新设计 World/Render/资产协议、恢复稳定 C++ SDK、全仓改命名空间、引入测试框架、
为目录各建一个库、修改第三方仓库。当前顺序为先完成 PLAN-118 P1，再优先执行本计划的测试精简，之后恢复 PLAN-118 P2/P3。
此前 PLAN-118 查询与线程测试修改已随构建恢复验证，文档状态已在 8a570c1d 同步。

## 基础目标收敛补充

已确认边界见 [ADR-121](../decisions/ADR-121-foundation-build-boundaries.md)。
先建立无数学依赖的 Core 实现目标（错误与 Fatal），拆出 Math/Events/Tasks 的真实使用要求，
迁移平台和独立消费者；不将字符串池和类型身份表静态复制到多个模块。
后续再评估 GUID、字符串标识等实现归属及独立符号导出，日志完整目标暂未收敛。
此阶段不代表 Core 全部迁移或 PLAN-119 完成。

## 初始核查记录（2026-11-16）

按本轮工作区静态扫描，engine/include 有 173 个 .h/.hpp，engine/src 有 48 个头文件、
107 个 .cpp；tests 有 213 个 .cpp。这些是文件数，不是有效 CTest 数或质量指标。

- engine/include/gneiss/engine 已形成对外包含根，但仍混有 resource_cache、main_loop_wakeup、
  serialization/detail/json_adapter 等实现。resource_cache.hpp 相对包含 detail/asset_entry.hpp，
  暴露目录与实现目录拆开后该包含关系需修复，不能靠添加所有私有搜索路径掩盖。
- meta/registration.hpp 直接包含 meta/detail/type_ops.hpp；serialization/detail/typed_decode.hpp
  包含模板实现。不能仅依据 detail 名称认定它们应从 include 删除。
- engine/CMakeLists.txt 仍列 engine/core/... 等旧相对源路径，版本头输入仍指向 src/engine；
  私有包含根仍有 PROJECT_SOURCE_DIR/src。原 src/CMakeLists.txt 已基本清空。
- cmake/targets/internal_interfaces.cmake 部分清单已改成 engine/include，但 core/tasks 等仍引用旧路径。
  internal_headers.cmake 通过 COPYONLY 和哈希目录复制契约头，现有真实 include 根出现后职责重叠。
- logging 的 CMake 已位于 engine/src/core/logging，却仍声明 file_sink.cpp 和旧 platform/logging
  头路径，需按实际文件与职责核对，不能仅修改目录前缀。
- prefab_runtime_instance、scene_instantiation 测试直接编译多份引擎 .cpp，又链接 gneiss::engine；
  后者还读取前者 SOURCES 作为源清单。测试依赖生产实现及另一个测试的构建细节。
- 根 CMake 仍有 package/export/install 规则；include 的存在不能自动解释为全部头均可安装。

## 头文件规则

include 表示供 Engine 外部的仓内调用者使用的原生 C++ 契约；不承诺稳定 API/ABI，不另造包装 SDK。
保持当前 <gneiss/engine/...> 包含前缀，本次不再进行路径扁平化；C++ 命名空间仍为 gneiss 及领域子空间。
目录名不要求与命名空间层级一致。

1. Editor、Runtime、Tooling、Examples 的实际调用入口及必要类型放 include。
2. 仅 Engine 实现使用的类、控制块、适配器、调度状态及后端头放 src，与实现就近放置。
3. 契约头必须具备完整包含闭包，消费者不需要 engine/src。模板必需的 detail 可以放 include/detail，
   但不因此成为建议调用的业务 API；禁止把非模板内部访问器混入这个例外。
4. 测试需要访问不是公开头的理由；私有测试获得明确的局部包含路径。
5. GNEISS_API 表示符号可见性，不单独决定文件位置。共享库跨模块实际调用仍须正确导出。
6. 第三方依赖按真实包含关系传播：数学和模板必需依赖要正确传递，Granit/libuv/yyjson 等后端实现
   保持私有；不能把必需依赖标 PRIVATE 后让消费者手动补齐。

### 首轮文件归属清单

| 文件/分组（相对 engine/include/gneiss/engine） | 目标及理由 |
| --- | --- |
| error、result、export；core/types 下 guid/ref/value/string_name/entity_id/type_id/rid；math | 保留 include：共享值与调用契约 |
| meta 的 type/property/instance/registry/registration/views/ops | 保留 include：查询、注册、自定义能力适配是实际扩展入口 |
| meta/detail/type_ops、core/detail/canonical_type_name | 保留模板所需 include 闭包，标明非业务入口；先查直接和传递包含 |
| serialization 的 context/reader/writer/json/serialization/value/diagnostic/policy | 保留 include：格式入口和自定义读写器契约 |
| serialization/detail/typed_decode | 若公开模板继续使用则留 include；不为隐藏它复制模板或改成类型擦除 |
| serialization/detail/json_adapter/context_access/traversal | 迁 src；先迁移仓外直接消费者到已有格式接口，确有格式扩展需求再抽取不含 yyjson 的契约 |
| function/scene 的 world/game_object/scene_instance、组件、组合/实例化入口 | 保留 include；world_impl、hierarchy 存储、component_access 等留 src |
| function/render 的 service/commands/frame_request/相机及绘制输入 | 保留 include；executor/scene 存储、resource_storage、Granit backend 留 src |
| function/asset 的 manager/request/batch/loader | 保留实际加载及自定义 loader 契约；内部协议只在确有消费者时保留 |
| function/asset/resource_cache | 迁 src，与 asset_entry/load_operation 同属内部加载记录实现 |
| function/asset 的 reference_context/loading/render_resource | 逐调用者核查；不因名称统一保留，只有跨模块必需入口才留 include |
| function/application/application/configuration | 保留 include：宿主装配入口 |
| function/application/main_loop_wakeup | 迁 src：Application 私有循环协调 |
| resource 的 asset/texture/mesh/material/scene/prefab 和 description/data | 保留 include：逻辑资源、描述与导入器生成内容契约 |
| resource 中 binary/decoder/validation、core/sha256 | 按 Tooling 等真实消费者决定；被导入器实际使用的内容格式接口保留，PNG 解码等内部实现迁 src |
| core/types/rid_table | 迁 src：服务槽位容器；对外保留 rid 值，不暴露槽位与内部资源分类 |
| platform/files 的 service/types/path/stream/backend | 保留服务与可组合后端扩展契约；具体 libuv/browser 实现留 src |
| core/tasks 的 scheduler/wakeup_signal | 保留实际任务/唤醒契约；task_platform 等平台细节审查后迁 src |
| core/logging、events | 保留 logger/service/sink 与信号契约；队列、文件输出后端实现留 src |

此表是实施目标，不代表文件已完成移动；存在外部私有消费者时，先迁移调用链再收回头。
不保留旧目录转发头作为长期兼容层，用户尚未发布的源码接口可以直接迁移。

## 目标布局与调用方式

```text
engine/
  CMakeLists.txt
  include/gneiss/engine/
    core/                   # 含必要模板 detail
    resource/
    function/
    platform/               # 服务契约，不含原生后端实现
  src/
    core/
    resource/
    function/
    platform/
cmake/
  dependencies/             # 下载、版本、离线缓存适配
  targets/                  # 警告、输出目录、profiling 等通用规则
  package/                  # config/export/install 模板与规则
  scripts/                  # embed_binary 等生成工具
  testing/                  # CMake 本身与 package 的验收脚本
tests/                     # 统一布局见下节，不另维护一套引擎测试平铺目录
```

消费者只链接对应目标并包含 <gneiss/engine/function/scene/world.hpp> 等入口；
不手动添加 engine/src，不复制契约头到构建树。Engine 自身用 PRIVATE engine/src，
用 PUBLIC 的 BUILD_INTERFACE 暴露 engine/include；安装接口另行明确，不自动安装该目录。
初始化仍调用既有初始化/创建入口，正常使用保持原 API，业务失败仍由既有 result 表达，
关闭仍按现有所有者顺序执行；本计划不改变资源保活和销毁语义。

## 扩展范围：剩余模块与应用入口（已确认目标）

用户确认本计划同时整理剩余 src 与 apps，不受现有目录限制。按职责迁移，
不把目录迁移扩展为业务协议或运行时所有权重设计。目标结构如下：

```text
tooling/
  include/gneiss/tooling/{import,build}/
  src/{import,build}/
  assetc/main.cpp             # 与 assetc 应用装配及目标定义放在一起
  CMakeLists.txt
editor/
  main.cpp
  application.hpp/.cpp
  documents/
  assets/
  inspection/
  project/
    project_manager.hpp/.cpp # 未打开项目时的选择、最近项目及创建界面
    cli/main.cpp             # 独立工程命令行入口与命令处理
  runtime/                   # 子进程管理、编辑器侧会话和协议适配
  ui/{panels,viewport,dialogs}/
  resources/
  CMakeLists.txt
runtime/
  main.cpp
  application.hpp/.cpp
  scene/
  assets/
  inspection/
  ipc/
  CMakeLists.txt
project/
  include/gneiss/project/
  src/                       # Editor/Runtime 共用的项目配置契约与读写
  CMakeLists.txt
protocol/
  include/gneiss/protocol/
  src/                       # 双方共享消息及编解码，不含应用命令执行
  CMakeLists.txt
tests/
  CMakeLists.txt             # 引擎与编辑器测试装配
  engine/
    CMakeLists.txt           # 启用领域分组与跨领域装配
    core/ meta/ serialization/ asset/ scene/ render/
    application/ platform/ input/ log/ headers/
    task_web/                # 独立 Emscripten 浏览器宿主
  tooling/ editor/ runtime/ project/ protocol/ integration/
  support/                   # 跨领域夹具与等待辅助
  data/ fixtures/            # 共享输入资产与进程夹具
```

Engine 及 cmake 布局沿用上节。Editor/Runtime 是应用，内部头就近放置，不强行建立
include/src；内部模型库按真实复用和测试需求保留，不按每个文件夹建立目标。
Tooling、Project、Protocol 被多个模块使用，提供明确 include/src 边界。
完成全部消费者迁移后删除空的根 src、apps 和泛化 host 层，不保留转发头。

入口核查事实：apps/project/main.cpp 调用 run_project_cli，当前支持 create、export、
package、verify，无参数打印用法；它不是项目选择窗口。选择窗口由
src/host/editor/project_manager.cpp 实现，editor_application.cpp 在未指定项目时调用
run_project_manager。两者均属于编辑器的项目工具，但保持独立入口和职责。

目标依赖与命名：

- gneiss_tooling / gneiss::tooling 替代 gneiss_asset_import_sdk，暴露真实 include；
  对外头依赖 Engine 类型时 PUBLIC 链接 gneiss::engine，导入器第三方依赖 PRIVATE。
- gneiss_assetc 只链接所需实际库，删除 gneiss_tooling_interface 及复制清单。
- gneiss_project_config / gneiss::project_config 承担共享配置；保留 gneiss_project
  可执行名称及命令行为，避免此前建议的共享库同名冲突。
- gneiss_protocol / gneiss::protocol 承担共享协议；允许依赖当前 Engine 值与描述类型，
  不依赖 Editor/Runtime 的实现，不额外复制一套协议对象模型。
- gneiss_editor、gneiss_runtime 维持可执行目标；编辑模型、UI、工程操作和 Runtime 协议处理
  的内部库仅在真实多消费者或测试需要时保留，不强制合成一个库。
- 项目选择 UI 与 CLI 共用的工程操作可归 editor/project 内部库；共享配置库不能反向依赖
  编辑器 UI、Tooling 或进程启动流程。平台 IPC 传输和进程能力仍归 Engine。
- 应用库不编译 main.cpp、不反向依赖可执行目标；安装、资源复制和打包规则随可执行目标移动。

迁移顺序：Tooling 契约与 assetc → 共享配置/协议 → Editor 与工程 CLI → Runtime →
测试分组与旧路径清理。每组先迁移源码和全部消费者，再验证对应实际目标及行为测试。
CMake PRIVATE/PUBLIC 依据头依赖判断；测试不得通过全局 src 包含路径穿透边界。
P2/P3/P4/P5 按此扩展范围验收；目录迁移已实施，边界与构建验收状态见顶部摘要及后续记录。

## CMake 目标与职责

| 层次 | 目标处理 |
| --- | --- |
| 引擎入口 | 保留 gneiss_engine / gneiss::engine；统一管理自身源清单、生成版本头、导出定义与依赖 |
| 仅头契约 | 合并 value/native/result 等复制清单目标；有真实独立消费者的头目标可保留，但直接引用源 include，不复制 |
| 已有独立能力 | tasks、events、IPC、I/O、process、dynamic_library 按实际独立消费保留；不是每个命名空间都建库 |
| 日志后端 | 文件输出实现与 logging 服务契约分开；spdlog 只属于实际输出实现，不让所有消费者链接它 |
| 平台/依赖 | Granit、libuv、libassert、GLM 等由依赖脚本提供目标，各实现目标声明 PRIVATE/PUBLIC，不全局传播 |
| 宿主和工具 | 根目录按 Engine、Tooling、Editor、Runtime、Project、Protocol 装配；按下述已确认布局迁移 src/host 与 apps，入口随可执行目标归属 |
| 共享内部编译单元 | 仅在两个以上实际构建消费者需要时考虑 OBJECT/STATIC；先证明避免重复状态，不为缩短清单随意拆库 |

每份生产 .cpp 有一个明确的编译归属；不得在同一最终进程中既从 DLL 又从测试副本拥有 registry、
string_name 池或服务状态。测试私有实现可以使用一个专门的静态测试组成，但该测试不能再链接
含同一实现的共享 Engine。不要用 GNEISS_BUILDING_LIBRARY 将测试伪装成库实现来补导出缺口。

engine/CMakeLists 可通过少量分组 target_sources 或子目录分摊清单，不要求每组建立库。
禁止从测试目标的 SOURCES 推导另一个目标。移除内部头复制机制前，先建立 include 闭包与负向边界验证。
根 CMake 保留选项、依赖配置顺序、add_subdirectory 和顶层安装入口；通用函数集中维护，
不创建隐藏大量目标副作用的宏体系。依赖脚本拆目录后同步 CMAKE_MODULE_PATH、相对路径和离线测试。

## 测试整理

遵循[测试指南](../guides/testing.md)与 [PLAN-114](PLAN-114-test-maintenance.md)，不设测试数或覆盖率配额。
本计划负责布局迁移涉及的构建和重复覆盖；PLAN-114 保持测试行为维护规则的权威入口。

- tests/CMakeLists 负责启用分组；各分组 CMakeLists 就近声明测试及真实依赖。
- 可增加一个很小的 gneiss_add_test 辅助，统一警告、输出目录、add_test；不默认链接 Engine/GPU，
  SOURCES/LIBRARIES/LABELS 明确传入。不为每条断言创建可执行文件，也不强制全仓一个巨型测试程序。
- 优先审计 native_world_lifecycle/world_service、Scene/Prefab 实例化与 application 装配、
  Editor scene_loader/prefab_edit 的重复：底层保留对象和生命周期语义，上层仅保留装配与编辑特有行为。
  必须建立“旧断言 → 保留测试/需求已取消”的映射后删除，不能仅凭名称相近合并。
- 清理 support/value_result 和 scene_fixture 等兼容辅助中只适配已删除 API 的部分；
  保留有意义的资源夹具，不让辅助再次实现资产管理、场景服务或生产状态机。
- 边界测试检查真正的 include/链接依赖；删除只固定旧目录、旧私有类名、精确内部步骤的文本检查。
- 采用 cpu/integration/gpu/death 标签；benchmark 与日常正确性分开。死亡测试独立子进程并抑制弹窗。
- 优先复用既有测试框架/入口，本次不引入新测试框架。批量编译契约头可使用一个消费者目标，
  每个头独立编译单元，避免先包含其他头掩盖缺失依赖。

## 实施阶段与验收

- [x] P0：静态盘点迁移状态，明确头归属、目标原则和测试清理边界。
- [x] P1：恢复最小构建闭环。修复 Engine 源清单、生成版本头、任务/平台目标、根装配及 tests 旧路径；
  保留用户当前内容，先得到一个能配置构建的最小闭环，再归类头文件。
- [x] P2：完成 include/src 分界和所有实际消费者迁移；移除重复头复制清单，检查模板包含闭包与第三方传播。
- [x] P3：收敛 CMake 目标归属、重复编译和脚本目录；Shared/Static 分别检查导出和链接，消除双份全局状态。
- [x] P4：按领域整理测试目标与辅助；先迁移必要覆盖再删除冗余，实现准确标签与最小测试入口。
- [x] P5：同步 coding-style/building/架构与边界文档，定向回归后检查旧路径、旧复制清单、旧测试组成退出。

P1 的最小闭环：离线配置 → Engine → minimal 示例 → 一项 World 与一项 Meta/序列化 CPU 测试。
P2/P3 覆盖 Runtime、Editor、Tooling 和两个示例的真实消费者构建；检查私有头不能仅通过 Engine
链接目标被外部包含。需要 GLM/EnTT 等头依赖的契约必须自行传递正确配置。
P3/P5 验证共享与静态构建及现有 package/install smoke；不把“可安装库”写成“完整 SDK 已发布”。
资源后端路径变化运行对应测试；纯文件移动不无条件启动全 GPU 压力矩阵。未执行平台逐项注明。

每组验证通过后聚焦本地提交。用户大范围移动与此前接口修改按实际内容审查，不执行批量覆盖或回退；
本轮计划提交只包含新计划与索引。旧 PLAN-118 的错线程死亡测试已移除对 Windows 头的依赖并验证通过。
相机已按用户当前数据组件语义调整测试：直接写入后检查 valid，非法投影拒绝且不修改输出；
未恢复已删除的相机 setter。

## 当前验证与过渡项

P2 首批完成：resource_cache、main_loop_wakeup、rid_table 已从 include 移到 src；
生产调用者全部位于 Engine，6 个相关内部测试目标显式获得 PRIVATE engine/src。
新增普通 Engine 消费者边界测试，检查三个头的原公开路径与当前私有路径均不可见，
同时包含 Application、AssetManager、World 契约确认正常使用不需要实现目录。
Windows Clang Debug 构建通过；main-loop-wakeup、rid_table、render_executor、prefab_description、
asset_manager、render_asset_loader、engine-header-boundary 共 7 项回归通过。
本批未删除既有行为测试，未解决测试重复编译生产源码，P2/P3 均仍未完成。

P2 序列化内部头已收拢：context_access、traversal 移至 engine/src/core/serialization/detail，
生产包含与 CMake 清单同步；公共模板所需 typed_decode 仍保留 include。普通消费者边界测试
同时包含 JSON 与通用序列化接口，检查这两个内部头不可见。Windows Clang Debug 构建及
serialization、serialization-archive、reflection-json、engine-header-boundary 共 4 项测试通过。
json_adapter 也已移至 engine/src：Host IPC 的 property/inspection 改用公开 JSON 编解码入口，
Host 自身负责嵌入已编码片段；读取 DOM 片段时转为文本再解码，不改变协议字段格式。
该边界有局部转码与临时缓冲开销，尚未做性能测量；不为免除转码向普通消费者暴露 yyjson 后端类型。
原 JSON 分配失败测试保留局部 PRIVATE engine/src，nested_json 移除不必要的内部头依赖。
修复独立 IPC 库的私有包含根，并停止传播其实现目录。Windows Clang Debug 下 IPC 属性协议、
领域协议（含非空多态相机组件）、IPC transport、reflection-json、engine-header-boundary 共 5 项通过。
此次配置启用 Runtime 以构建真实 Host IPC 消费者；未验证完整 Runtime/Editor 程序及其他平台。


- Windows Clang Debug，离线配置成功；Engine、minimal、world_service、reflection_boundary 构建通过。
- gneiss.minimal-example、gneiss.world-service-boundary、gneiss.world-thread-contract、
  gneiss.reflection-boundary 共 4 项通过。
- 引擎接口已直接使用 engine/include，原 value/native/result 目标名暂时保留为直接包含的 INTERFACE；
  Tooling 已改为真实 include 契约，头复制脚本及 gneiss_tooling_interface 已删除；其余接口目标继续在 P3 审计。
- 旧 core-boundary 等路径扫描规则仍需迁移；本轮未运行它们，不用空扫描声称边界验收通过。
- 其余目标的私有头访问、模板闭包、重复实现与共享/静态安装验收留待 P2/P3。
- 本轮纳入用户已有目录迁移及必要包含修复；没有回退用户的相机数据结构调整。

Tooling/assetc 已迁移：对外头位于 tooling/include/gneiss/tooling/{import,build}，
实现及私有头位于 tooling/src；assetc 的 main、装配与 CMake 合并到 tooling/assetc。
gneiss_tooling（别名 gneiss::tooling）PUBLIC 依赖 Engine，第三方导入与编码库保持 PRIVATE。
Editor 及测试消费者同步更新；普通 Tooling 测试不再添加仓库 src 根，白盒测试局部申请 tooling/src。
删除 internal_headers.cmake 复制机制，不保留旧路径转发头。修复原私有 sha256 转发头中的
既有 gniess 包含拼写错误。本次不改变导入格式或业务 API，asset_import_sdk 类型名仍保留。
Windows Clang Debug 下 assetc 与相关目标构建通过，14 项定向回归通过，包含 assetc inspect/cook。
Editor 的 gneiss_editor_assets、gneiss_editor_host 实际消费者构建通过，尚未验证完整图形界面。
资产监视器既有 uv_loop_access 依赖已修正为该目标 PRIVATE engine/src；该跨模块私有依赖
须在后续 Editor/平台边界整理中收敛，不能据此宣称所有消费者已摆脱引擎私有头。

共享配置与协议已拆分：原 host/common 的项目配置归 project，运行日志协议与 IPC 契约归
protocol；后者不再通过项目配置库提供。目标分别为 gneiss_project_config、gneiss_protocol，
提供 gneiss::project_config、gneiss::protocol 别名，只公开各自 include 根，不公开实现目录。
公开头统一使用 gneiss/project、gneiss/protocol 前缀；本批保留现有 gneiss::host 命名空间，
不改变消息格式或函数签名。反射 JSON 拼接辅助仍私有。原 host/common 的源码及 CMake 已退出。
测试定义与源码分别迁到 tests/project、tests/protocol，保留原测试名与覆盖，移除多余 src 根包含。
Windows Clang Debug：10 项配置/协议回归通过；Editor 宿主、Runtime IPC、完整 Runtime 与
工程 CLI 构建通过。Runtime 日志头的旧包含路径同步修正；未启动图形界面或验证跨平台安装。
旧 core-boundary 文本规则仍待 P5 整体更新，本批不以旧规则扫描作为验收依据。

Editor 源码与入口已归入根 editor：documents 下收纳文档、编辑命令与组件编辑注册；
assets 为导入协调，inspection 为运行对象镜像与日志模型，ui 下分 panels/viewport/dialogs；
project 包含工作区、项目选择界面与 cli，runtime 包含子进程与编辑器侧 IPC，字体与声明归 resources。
Editor main 和 application 就近放置；工程 CLI main 归 editor/project/cli，仍输出 gneiss_project。
editor/CMakeLists.txt 定义宿主与可执行目标，libraries.cmake 保留现有编辑模型等内部库；
原宿主测试定义移至 tests/editor/host_targets.cmake，测试源码暂保留 tests/host/editor，后续 P4 合并。
所有生产和测试包含路径已按迁移更新；旧 Prefab 白盒测试显式申请 editor/engine 私有根，
其重复编译生产源码仍待 P3 处理，不因目录移动视为已解决。
Windows Clang Debug：完整 Editor 与工程 CLI 构建通过；8 项定向回归通过，覆盖运行面板、
文档编辑、工作区、保存、相机、资产导入、IPC 事件与 Prefab 覆盖；未运行 GUI/GPU smoke。
本批修复 Editor 的旧 console_sink 包含路径，未改变编辑操作、协议或资源所有权。

Runtime 源码及入口已归入根 runtime：application/main 就近放置，scene 负责场景加载，
assets 负责来源重载，inspection 负责对象检查与属性编辑，ipc 收纳会话及消息处理。
原 src/host/runtime 和 apps/runtime 路径退出；对应测试源码、脚本与 CMake 归 tests/runtime。
生产目标直接依赖 Engine、Project、Protocol，移除 Runtime 的 native_interface 间接入口；
日志测试修正为依赖日志协议所属 Protocol，测试定义不再混入生产 CMake。
Windows Clang Debug：完整 Runtime 与 9 个测试目标构建通过，10 项定向测试通过，覆盖
IPC、检查、加载、属性、资产重载、日志、启动诊断及 Scene/Prefab 结构重载。
自定义组件测试局部申请 engine/src；共享网格夹具仍使用 Editor 绑定值类型，局部申请 editor，
该测试耦合及 runtime_logging 的重复编译留待 P3/P4 收敛。未验证 GPU、安装与其他平台。

Editor 测试路径已收拢：tests/host/editor 的源码、脚本与进程夹具移入 tests/editor，
宿主及示例工作流目标同步引用新位置；删除 Editor 测试对旧仓库 src 根的包含配置和空循环。
未删除测试断言、改变测试名或恢复旧 API。Windows Clang Debug 下受影响的 37 个 Editor
测试目标构建通过，15 项定向回归通过，覆盖文档、Prefab 编辑、场景加载、保存、工作区、
历史、相机、运行镜像、属性、IPC、日志及导入。图形窗口、外部场景与进程工作流未运行。
P3 的重复生产源码编译、P4 夹具跨层依赖及 P5 旧边界检查仍未完成。

共享网格夹具已解除 Editor 依赖：直接使用 mesh_component 保存资源引用，Runtime 结构
重载测试删除 editor 私有包含根；Scene/Asset 消费者同步更新。修正两处旧白盒测试的
engine/src 私有包含路径，以及 scene_instance 中将合法 camera.valid() 判为失败的反向断言。
Windows Clang Debug 下 5 个受影响目标构建及回归通过，覆盖场景加载、纹理加载、场景实例、
实例化与结构重载；未运行 GPU 或其他平台。
重复生产源码审计发现：Prefab 覆盖测试调用未导出的覆盖校验，场景实例化夹具直接调用私有
解码器；直接改为链接库会失败，混合编译又会产生导出符号冲突。本轮保留原编译策略，
后续须先让夹具通过实际加载/覆盖入口验证，不能用扩大 DLL 导出或删除断言绕过。

## 测试辅助层与正式 API 审查（2026-11-17）

本节补充 P3/P4 的实施顺序：先消除测试中的业务替身和旧接口适配，再收敛重复编译；
不为维持旧夹具扩大 DLL 导出。下面是源码审查结论与待实施方案，不表示 API 已经落地。

| 现有辅助 | 源码事实 | 处理方案 |
| --- | --- | --- |
| mesh_objects.hpp：read_mesh / mesh_target | World、Scene、entity 三项定位后借用组件，再构造副本；组件缺失被转换为空组件 | 调用方保留 game_object，直接 try_component；需要副本时显式复制。缺失组件与资源为空分别断言，不新增读取副本 API |
| mesh_objects.hpp：assign_mesh / spawn_mesh | 前者混合缓存查找、更新与通知；后者经过描述构造和组件装配 | 普通运行对象测试直接加载资源并 add/修改组件；组件装配测试才使用 component_context 和描述，不把两种目的混在通用工具里 |
| scene_access.hpp / scene_node_info.hpp | 包装 create_empty、get_node_count、find_node、reparent_node 等旧调用；create_empty 校验 UUID 后不保存它 | 创建、数量、层级操作直接使用 World、SceneInstance、GameObject；来源定位只保留实例化回执的查询，不重新给运行对象添加描述身份 |
| value_result.hpp / codec_result.hpp | 将 result 转成输出参数和错误码，还适配 entity_id | 触及的测试改为直接断言 result 并取值；保留有明确断言语义的辅助，不将输出参数适配搬进正式 API |
| prepare_render.hpp | 自定义 prepared_batch，调用私有 decode/adopt，并手动解析材质依赖 | 加载行为测试使用内存文件后端与正式 Asset Manager；编解码单测直接测编解码，不复制完整加载流程 |
| scene_instantiation.hpp / scene_application.hpp | 组合、创建场景、失败清理等测试编排混在便捷入口 | 先区分实例化契约测试和 Application 装配测试，分别调用对应入口；清理使用测试作用域工具，不恢复另一套场景服务 |
| async_asset / async_document / async_editor_save 等 | 等待请求、驱动宿主执行器与完成队列 | 保留测试驱动，但等待目标必须是明确请求完成；底层服务测试可以显式驱动，Application 测试应使用宿主 update，不让每个用例协调内部阶段 |
| memory_file_fixture、throws、terminate_process、logging | 提供可控环境、异常退出和日志捕获 | 保留；只有实际重复与无调用时再合并或删除 |

### 组件修改入口的候选契约

现状：game_object 已提供 try/add/remove_component 和 notify_component_modified；
component_registration.cpp、scene_update.cpp 的生产路径也存在赋值后手动通知。
因此这是真实使用问题，不只是测试语法偏好。候选调用：

```cpp
if (const auto* component = object.try_component<mesh_component>()) {
  auto saved = *component; // 需要独立值时显式复制，资源引用仍共享。
}
// 待实施 API；不在本次审查中新增。
auto status = object.modify_component<mesh_component>([&](auto& component) noexcept {
  component.material = material;
});
```

建议约束：对象有效、所属线程为前置条件；缺失组件返回 not_found 且不调用回调；
回调只修改当前组件，不增删组件、不销毁对象、不重入结构操作；正常完成统一发出修改通知。
先评估 noexcept 且返回 void 的回调能否覆盖实际调用，以避免异常后部分修改与漏通知；
需返回错误的校验应在写入前完成，不承诺复制回滚或事务。原裸借用通知入口是否保留，
须检查所有生产消费者，不能仅根据测试删掉。正式实现前补充新增、修改、失败、销毁完整用例。

### 实施与退出条件

1. 先迁移组件读取和场景查询的一条完整测试链路，不增加新业务 API；删除已无消费者的包装。
2. 再收敛生产组件修改与通知入口，测试验证实际同步消费者可见的结果，不固定内部调用次数。
3. 将 prepare_render 的加载行为消费者改为内存后端加正式加载；逐个删除私有 decode/adopt 依赖。
4. 上述依赖退出后，删除测试重复编译生产源码与 GNEISS_BUILDING_LIBRARY，验证真实库链接。
5. 每批记录保留的白盒理由、行为覆盖、未运行项；不以辅助函数或测试数量作为验收指标。

本轮仅完成源码审查和方案记录。重复编译、旧场景包装及候选修改接口均未完成。

组件读取与场景查询首条链路已迁移：scene_structural_reload_test 直接保留 GameObject，
通过 parent、try_component、SceneInstance::size/unload 验证重载，不再使用 scene_access、
read_mesh、read_component_value 和 result 输出参数适配。来源查询限当前加载回执的局部查找，
没有新增运行身份或通用查询包装。保留身份不变、退役句柄、跨应用拒绝、缺失视觉内容、
损坏描述及卸载后的行为检查。
scene_load_application_test 同步直接查回执、借用组件、显式复制资源引用，更新后重新借用；
解除 scene_access/mesh_objects 依赖。两项目标 Windows Clang Debug 构建及定向回归通过。
共享包装仍有其他消费者，本轮未删除其定义，也未新增候选 modify_component API。

2026-11-17 后续实施：read_mesh 已删除，消费者直接借用 mesh_component，需要跨对象销毁
保留资源时显式复制。assign_mesh/spawn_mesh 和旧场景查询仍有消费者，第一项尚未全部结束。
game_object::modify_component<T> 已实现 noexcept/void 回调契约，缺失组件返回 not_found，
不执行回调；成功后通知。相机/网格装配与场景更新生产路径已迁移；裸借用与显式通知仍保留，
用于既有调用和重试行为，不扩大到结构修改或事务。渲染同步测试验证通知实际到达消费者。
场景实例化测试改为正式资产加载，不再依赖 prepare_render；其 CMake 不再复制另一个测试
的 SOURCES、不编译生产实现、不定义 GNEISS_BUILDING_LIBRARY，直接链接 Engine。
async_render 仅驱动和等待正式单资源请求，移除私有 resource_load_group 依赖；另一个
重载批次白盒测试仍直接使用该组并显式包含私有头，不以传递包含隐藏依赖。
Windows Clang Debug：场景实例、实例化、结构重载、纹理加载、资产加载、World 渲染绑定
共 6 项回归通过。未运行 GPU、静态库和跨平台配置。prepare_render 的其余渲染测试消费者
及旧 scene_access 包装尚待迁移，三项整体工作未全部完成。

渲染资产测试继续迁移：材质参数用例删除 prepare/adopt 旁路，保留真实加载后的参数、
缓存身份、解码次数及重载身份验证；删除仅针对测试自建批次大小、字节估算的断言。
平铺 UV 用例通过正式请求分别加载 JSON/二进制网格，比较实际资源内容；失败重载时
持有原资源并检查内容快照不变，取代对测试批次清空的断言。
Windows Clang Debug 构建及 render_asset_loader 回归通过。纹理编解码/生命周期用例
仍有 prepare_render 消费者，尚未删除该辅助文件；未运行 GPU 或跨平台验证。

纹理服务测试进一步分离职责：材质预留/完成/释放改为正式异步 load/take，结果作用域结束后
再释放最后一个引用并检查回收；网格数据移动测试直接调用解码器，保留缓冲移动与渲染副本
生命周期检查。纹理来源摘要测试直接验证描述/载荷解码记录，不构造材质递归准备批次；
增加重复输入摘要一致检查，保留载荷身份、大小、摘要变化断言。此类解码记录测试仍为白盒。
修正该目标旧私有包含根。Windows Clang Debug 构建及 texture-load-service 回归通过。
prepare_render.hpp 的 decode/adopt 白盒辅助及其他消费者仍存在；未宣称辅助层已全部删除。

最后一个批量准备消费者已退出：删除测试专用 prepared_batch 和 prepare_render_for_test，
不再在测试辅助层递归加载材质依赖、排序、累计预算或维护取消状态。纹理变体用例直接验证
解码结果及接入后的缓冲身份、内容快照；移除测试辅助函数自己的取消回调次数和精确读取次数
断言。正式请求取消仍由 texture_load_service_test 的共享请求、放弃和取消用例覆盖。
Windows Clang Debug 两个目标构建、render_asset_loader 与 texture-load-service 回归通过。
prepare_render.hpp 暂仍承载 decode/adopt 白盒辅助，未删除该文件；旧场景包装与重复编译生产
源码仍待后续处理。未运行 GPU、静态库及跨平台配置。

Prefab 结构重载测试直接使用加载回执定位 game_object，删除场景全量枚举、排序和
scene_node_info 副本转换；保留同源双实例、重载身份、覆盖恢复及场景卸载检查。
实例化夹具直接通过 world.create_scene / scene_instance.unload 管理创建失败的清理，
删除测试包装额外要求的场景 UUID；async_application 不再传递包含 scene_access。
共享夹具回归发现 component_adapter 仍引用旧 src 私有包含根，已修正为 engine/src；
该测试为自定义组件装配白盒，未扩大引擎导出接口。
Windows Clang Debug 五个相关目标构建通过，Prefab 结构重载、场景实例、实例化、场景 Prefab
实例及组件装配五项回归通过。未运行 GPU、静态库及跨平台验证，其他 scene_access 消费者仍待迁移。

2026-11-17 已确认顺序：先完成 [PLAN-118](PLAN-118-error-contracts.md) P1 的 World/Scene
契约收尾，随后优先处理本计划的旧 scene_access 包装、重复行为验证和重复编译生产源码的目标。
Meta/Render/Prefab 测试随 PLAN-118 P2/P3 对应接口迁移整理，不提前批量重写。
本轮不按测试数量删减；保留生命周期、外部输入、异步关闭及跨模块装配验证。
测试精简仍未完成，不因对象接口迁移结束而关闭本计划。

### P1 接口稳定后的测试精简：Prefab 运行应用

2026-11-17：prefab-runtime-application 改为只编译测试自身并链接真实 Engine 与线程依赖。
移除重复编译的十二个引擎源码、GNEISS_BUILDING_LIBRARY、engine/src 私有包含目录，以及
直接 PNG/JSON/Granit/任务库依赖。测试直接包含 composition/scene_update 原生契约，删除
未使用的 scene_application/value_result 包装头及重复 entity_id 转换。
保留同源实例独立身份、改父与改名、循环拒绝、跨线程拒绝、删除及失步检测的全部行为验证。
同时删除 CMake 中不添加任何库的空 target_link_libraries 循环；未改动其他目标的有效依赖。
最终 Windows Clang Debug 配置、目标构建和 prefab-runtime-application 测试通过，
git diff --check 通过。未运行静态库、Release、跨平台或 GPU 验证；其他重复源码目标及
scene_access 消费者仍待逐链清理，本计划未完成。

### 描述序列化测试使用真实 Engine

2026-11-17：scene_description 与 prefab_description 两个测试目标移除六处重复的 Engine
源码编译，去掉 GNEISS_BUILDING_LIBRARY、旧 src/私有 engine/src 包含路径及直接 JSON/EnTT
依赖，保留 Engine 和异步夹具实际使用的任务库依赖。Prefab 测试移除未使用的缓存私有头。
仓库引用核查确认 prefab_author_address 只剩孤立测试使用，两个配套 UUID/身份校验函数
没有生产调用，因此删除旧类型、函数及其孤立断言，不为测试增加导出。当前覆盖地址模型、
序列化、层级校验、异步加载和弱缓存行为用例保留。
Windows Clang Debug 两个目标构建成功，scene_description/prefab_description 两项测试通过，
git diff --check 通过。未运行 Release、静态库和跨平台验证。
资产加载/纹理服务测试仍调用私有解码与资源接入辅助，不能直接照搬此次源清单删除；
这些白盒边界及 scene_access 消费者仍待清理，PLAN-119 未完成。

### 场景测试直接使用对象与场景句柄

2026-11-17：场景实例化、组件装配测试中有效场景的创建、卸载与子树删除直接调用原生 API，
不再为创建空场景制造无用途 UUID，也不通过 scene_access 将 void 生命周期操作包装回错误码。
scene_prefab_instance 完全移除 scene_access 依赖，直接持有 game_object 执行改名、改父和
删除，保留同源实例独立、子树失效、其他实例存活及最终实体回收检查。
async_document 在文档接管失败后直接卸载场景；已核查 adopt 失败仍由调用方持有场景。
Windows Clang Debug 四个目标构建通过，scene-instantiation、scene_prefab_instance、
component-adapter、editor.transform-gizmo-drag 四项测试通过；git diff --check 通过。
未运行完整编辑器测试、Release 或跨平台矩阵。scene_access 的旧查询及非法句柄模拟仍有
消费者，尚未删除该辅助类，后续应按真实查询与外部边界分别迁移。

### 测试目录与领域 CMake 归位

2026-11-17：完成本轮目录归位。原根目录的引擎测试及 core/meta/serialization/asset/scene/
world/render/platform/input/log/headers/task_web 归入 tests/engine，World 合并到 scene；
主循环唤醒用例从 tests/runtime 归入 engine/application。各领域 CMakeLists 就近声明目标，
根 tests/CMakeLists 只装配 Engine 与 Editor；原散落的材质编辑器目标回到 tests/editor。
共享 support/data/fixtures 保留原位置，更新相对包含、平台目标源路径、浏览器工作流和指南。
删除本文旧的平铺测试布局示意，以上述统一树为权威目标。

Windows Clang Debug 配置成功；迁移前后 CTest 均为 235 项，名称、超时和标签无增删变化。
16 个代表目标（包含头文件编译目标）构建通过，15 项定向运行测试最终通过，覆盖 Core、Meta、
序列化、资产 URI、场景、Render CPU 状态、平台动态库/UV、输入、日志、唤醒和材质编辑器。
验证同时修复动态库头遗留的多余 endif、测试 DLL 导出设置误加到 executable，以及 UV、
Render Scene、材质编辑器白盒测试的旧私有包含路径。git diff --check 通过。

尝试完整构建时仍被 integration/assets/file_service_test 的旧私有包含路径阻断；本轮未将
该测试的实现依赖迁移扩展为接口改造。未完成完整构建/测试、Release、静态库、GPU 和浏览器
运行验证。独立浏览器宿主已改用真实任务库目录及显式最小依赖，尚待 Emscripten 验证。
P4 的目录组织部分已落实；旧辅助、白盒边界、准确标签与剩余构建问题仍未结束，不能据此关闭
PLAN-119。平台 IO/IPC 测试目标仍由对应生产子目录声明，后续继续收回测试侧。

### 目录迁移的构建与目标归属收尾

2026-11-17：完成用户确认的三项收尾。文件服务测试确实使用内部内存后端，保留显式局部
engine/src 包含路径；IO/IPC 的全部测试与致命边界用例从生产 CMake 移到
tests/engine/platform/CMakeLists.txt，由对应生产目标是否存在决定启用。
测试根 src 搜索路径已清除，实际白盒依赖逐目标改为 engine/src；独立编译测试补齐接口库
依赖，PBR Shader 测试改用 engine/generated/pbr。未扩大 Engine 的公共头或导出范围。
minimal 示例同步迁移 local_transform 直接返回值的既有契约。

Windows Clang Debug 全量构建成功，最终构建无待执行项；43 项相关 CPU 测试全部通过，覆盖
文件服务、IO/IPC（含致命边界）、资产、场景、独立基础测试与编辑器组件/文档用例。
minimal 实际运行输出 updates=3, x=4。迁移前后 CTest 均为 235 项，名称、超时和标签不变。
活动测试/CMake/工作流中旧测试目录路径及 tests CMake 的根 src 搜索路径检查无残留；
IO/IPC 生产目录不再声明测试目标。git diff --check 通过。

本次三项收尾已完成；未运行全部 235 项、GPU、浏览器、Release、静态或跨平台矩阵。
此前记录的文件服务完整构建阻塞已解除。PLAN-119 的旧辅助精简、白盒依赖审计、完整标签和
其他平台验收仍未完成，不能将本轮闭环等同于整个计划完成。

### 测试目标 IDE 分组约定

2026-11-17：按用户要求将测试目标 `FOLDER "Tests"` 约定写入测试维护指南，AGENTS.md
与构建指南链接该规则。测试专用夹具库、头文件编译目标与基准目标一并适用，生产依赖不改归属。
补齐 Engine 头文件边界目标和独立浏览器宿主，两个基准目标从 Benchmarks 统一为 Tests；
顶层工程显式启用 USE_FOLDERS，作为子项目时不覆盖父工程设置。
Windows Clang Debug 配置通过，通过 CMake File API 核查当前 201 个测试构建目标均属于
Tests；235 个 CTest 用例的名称与属性完全不变。git diff --check 通过。
本轮仅改文档与 IDE 组织属性，没有新增源码行为测试，也未重复运行完整构建或运行测试。
独立浏览器目标仅核查源码设置，未执行 Emscripten 配置；PLAN-119 其余事项继续保持未完成。

### 场景装配测试退出旧查询包装

2026-11-17：component_adapter 与 scene_instantiation 两个测试完全移除 scene_access 包含和
调用。组件测试使用已有回执映射及正式 capture_components；实例化测试按回执区分普通对象和
Prefab 成员，直接使用 game_object、scene_instance::contains/size 验证，不依赖排序后的节点
索引。删除未使用的节点描述采集、只验证旧包装自身的查询错误断言和无操作间隔的读计数自比较。
非法场景借用转为正式 borrow 检查；跨线程、跨场景、组件身份、组件析构、资源保活及实例
失效行为继续保留。未新增业务辅助或改变生产 API。
Windows Clang Debug 两个目标构建成功、两项测试通过；git diff --check 通过。
本轮不重复全量矩阵。scene_access 仍被其他 Scene/Editor/Runtime/Tooling 测试使用，
其剩余迁移和删除仍未完成。

### 编辑器与工具消费者退出场景测试包装

2026-11-17：10 个 Editor、Runtime、Tooling 测试文件移除 scene_access 依赖。
创建、计数、销毁和卸载直接使用 World、GameObject、SceneInstance 原生接口；
卸载检查实际句柄失效与实体回收，非法 ID 检查使用正式 borrow 边界。
同时清除四个已无调用的包含，不新增替代包装，不改变生产 API。

Windows Clang Debug 对应 10 个目标构建成功，10 项测试全部通过；git diff --check 通过。
本轮未重复完整矩阵。scene_access 仍被 10 个测试/基准源文件使用，后续继续迁移，
在最后一个消费者退出前保留辅助文件；PLAN-119 尚未整体完成。

### 资源重载与保存测试直接使用对象和装配回执

2026-11-17：texture_load_application、component_edit、editor_save 移除 scene_access；
native_application 清除未使用的包含。纹理重载用例保存 scene.objects() 返回的借用句柄，
直接读取组件，不再排序实体或采集完整描述；两个对象的材质共享与外部改绑验证保持不变。
保存和组件编辑用例按已有装配回执中的来源 UUID 查找对象，不维护额外运行身份。
卸载后检查场景及对象句柄失效，未改变生产 API 或引入替代包装。

Windows Clang Debug 四个目标构建成功，对应四项测试全部通过；git diff --check 通过。
scene_access 剩余六个测试/基准源文件消费者，包含三个渲染用例和三个场景/编辑器用例。
本轮没有运行 GPU 基准或完整矩阵，PLAN-119 其余验收项保持未完成。

### 编辑器生命周期与渲染对象选择退出旧包装

2026-11-17：author_property_edit、component_lifecycle、asset_pixel 和
texture_load_window_benchmark 移除 scene_access。编辑器直接按装配回执验证属性和场景归属，
以句柄失效验证卸载；像素测试与窗口基准按实际组件选择对象，不再假定排序后的相机/网格索引。
自定义 movement_component 保留既有白盒读取：当前动态库未导出其 try_component 模板实例，
不为此扩大生产接口。

四个 Windows Clang Debug 目标构建成功，两个编辑器测试通过。两个 GPU 用例运行失败：
asset-pixel 在资源等待处失败，pbr-negative-scale 的可见性对照像素相同。
临时使用 HEAD 原版 asset_pixel_test.cpp 构建运行，对照也在相同检查失败；随后恢复本轮源码
并重新构建成功。该结果说明当前基线同样失败，不能认定 GPU 验收通过，根因仍待排查。
窗口基准仅构建，未执行交互基准。git diff --check 通过。

scene_access 仅剩 scene_instance_test 与 scene_load_baseline 两个消费者；辅助删除和计划
其他验收尚未完成。

### 旧场景测试包装退出完成

2026-11-17：最后两个消费者 scene_instance_test 和 scene_load_baseline 迁移完成，
删除 tests/support/scene_access.hpp 与 scene_node_info.hpp，测试源码不再引用旧包装及
node_query_detail。场景测试通过装配回执定位对象，直接检查对象属性、层级、组件与场景大小；
确需验证描述捕获时直接调用正式 capture_components。基准只保存相机借用与初始变换，
不再采集整套节点描述。

删除仅由旧辅助维持的契约：空场景强制描述 UUID、按排序索引查询节点、禁止删除有子对象的
父节点、重复卸载返回错误。原生有效接收者使用契约不通过失败返回测试；失效外部场景 ID
继续通过 borrow 验证。递归删除、槽位 generation、跨 World、线程边界、组件增删、
资源引用保活、装配失败后恢复等验证保留。删除测试自有 set_camera 及其自证参数校验，
使用正式组件添加入口。

Windows Clang Debug scene_instance 与 scene_load_baseline 两目标构建成功；
scene_instance、scene-instantiation、scene_prefab_instance、component-adapter 四项测试通过。
git diff --check 及旧包装源码引用扫描通过。场景加载交互基准未运行，未重复完整矩阵。
此前已记录的两个 GPU 基线失败仍未解决；旧辅助退出完成不代表 PLAN-119 整体完成。

### 重复编译、标签与安装验收首轮对账

2026-11-17：scene_load_preparation 删除三份重复生产源码及 GNEISS_BUILDING_LIBRARY，
action_map 删除重复 asset_uri 实现、构建库宏和不必要 yyjson 依赖，统一链接真实 Engine。
input_service、validate_action_map 未导出，保留明确的白盒编译；Prefab 覆盖测试尝试链接
scene_document 后暴露未导出函数与导出宏冲突，本轮保留原组合，未扩大生产导出。
Runtime 日志及其他白盒源清单仍需逐项审查，不能将重复清单机械删除。

为已审查的八个输入/序列化/场景测试补 cpu 与 unit/integration 标签；两个像素用例补
gpu/integration 和统一 60 秒超时。CTest 总数仍为 235，尚有 213 项未分类。
未分类测试不纳入 CPU 安全集合，标签不表示用例已经通过。

四个受影响目标构建成功。14 项定向验收初次 12 项通过，两项失败为旧边界扫描路径和
安装日志协议失配。安装脚本改用 log-v2、runtime 来源和 category 阶段后，安装/目录包/
Shipping/示例 smoke 整项重跑通过。core-boundary 仍因扫描根 src 无源码失败，须连同反例
迁移规则；本轮未关闭此问题。静态配置与跨平台验收未运行，GPU 基线失败仍未处理。
git diff --check 通过。

### 当前目录的架构边界检查恢复

2026-11-17：core-boundary 扫描 engine/include、engine/src、editor、runtime、tooling、
project、protocol 七个自有源码根。Engine include/src 归一为逻辑领域路径，保留真实文件诊断；
契约 gneiss/engine 前缀及内部 core/resource/function/platform 包含均参与原有依赖规则。
数学、Meta、Serialization、资源、Scene/Render、平台与宿主依赖检查继续生效。

同步迁移原有反例；移除已废弃 Host 目录专用规则/反例和禁止 function/application 目录的
过时断言，应用装配现在位于该目录。修正编辑器属性链及 Web fetch 实际路径。
反例使用独立随机夹具目录，避免先前失败残留污染新运行；新增 include/src 双根、当前包含
前缀、五个外层模块的诊断匹配，以及空树拒绝和合法应用装配正例。
子进程诊断显式 UTF-8，并归一换行空白，避免 Windows 编码和 CMake 自动折行误判。

当前仓库实际扫描 556 个源码文件；gneiss.core-boundary 与 core-boundary-negative 两项
均通过，git diff --check 通过。仅修改检查脚本与计划，未重复生产构建或 GPU 测试。
此前旧 src 扫描失败已解决；静态矩阵、完整标签审查及 GPU 基线问题仍未完成。

## 决策边界

已采用：根 engine 延续、include/src 按消费者分界、减少复制清单和重复实现、测试按行为价值清理。
本计划未授权改变稳定 ABI、安装全部 C++ 头或新增 Core 独立运行时所有者。
若实施发现必须拆出独立二进制 Core 才能解决真实多消费者依赖，先给出链接与单例归属方案并更新 ADR；
否则优先保留现有所有权。不需要用户再次为普通文件移动、源清单修复或测试去重逐项确认。

### 2026-11-17：CMake 分层与统一测试装配

- CMake 文件迁入 dependencies、targets、package、scripts、testing；安装/导出规则由
  package/install.cmake 集中维护，根 CMake 只保留装配入口。
- 测试统一经 tests/CMakeLists.txt 按模块选项启用；同步迁移脚本调用、生成依赖、配置夹具、
  CI 缓存键及文档链接，不保留旧路径转发层。
- 修正独立 task_web 测试工程根路径回溯层数；本轮未执行 Emscripten 构建。
- Windows Clang Debug 配置、全量构建通过；11 项相关测试通过，含安装 Runtime smoke、
  cook/import、依赖/版本缓存、边界及反例、Debug/Release 断言。
- 迁移前后均为 235 项 CTest，名称集合、标签/超时等属性一致；201 个测试构建目标均属 Tests。
- 尚未完成：接口依赖目标收敛、白盒重复编译审核、标签补齐、静态与跨平台验收；
  两个既有 GPU 基线失败不在本轮宣称修复。PLAN-119 保持实施中。

### 2026-11-17：移除转发接口目标

- 删除 native/value/result/task/log_output 五个 interface 目标和 internal_interfaces.cmake；
  值类型要求合并到既有 gneiss_core_values，Tasks、Events、文件日志库直接声明使用要求。
  已链接 Engine 的消费者删除重复接口依赖，独立白盒测试按实际需要链接基础目标。
- gneiss_fatal 保留独立实现库，定义回归 engine/src/core/CMakeLists.txt；异常实现与宿主安装
  terminate handler 的语义不变，底层任务和 IO 不反向依赖 Engine。
- 修正 PACKAGE Granit 下静态 Engine 的配置错误：TARGET_RUNTIME_DLLS 仅用于共享 Engine。
- Windows Clang Debug 全量构建、28 项基础回归及共享安装 Runtime smoke 通过；235 项测试名称不变。
- 静态 Debug 使用已安装 Granit PACKAGE 配置通过；fatal/tasks/math/events/file sink 五个测试目标
  构建通过，对应 11 项测试通过。首次宽泛筛选误选两个未构建的 fatal-render 测试，均因可执行文件
  不存在失败；精确筛选后通过，不代表静态渲染验收。静态完整 Engine、安装以及 Web 验收仍未完成。

### 2026-11-17：小型 Core 与数学依赖分离

- Core 静态实现库合并错误文本和 Fatal，删除旧 gneiss_fatal/gneiss_core_values 目标；
  新增 core/math/events/tasks 的领域别名。数学使用要求单独传播，平台及任务不再间接依赖 GLM。
- error_message 改为 Core 的普通链接符号，不再从 Engine DLL 导入；宿主通过 Engine 的构建树
  PUBLIC 依赖获得 Core。静态安装 Engine 包含对应基础实现，不新增 SDK 导出目标。
- native_result 现有测试改为只链接 Core，验证独立错误文本/结果能力；不新增重复测试。
- Windows Clang Debug 全量构建通过，30 项基础及安装 Runtime smoke 通过，235 项测试名称不变。
  静态 Debug 六个基础测试目标构建通过，对应 12 项测试通过。
- 编译命令确认独立 Core/Tasks/IO/进程目标没有 GLM 路径；同一 IO 源文件编入 Engine 时仍获得
  Engine 自身数学依赖，不能把源码复用误判为平台目标仍依赖数学。
- GUID、字符串驻留表、类型身份表、完整日志实现尚未迁移；共享状态归属保持在 Engine。
  静态完整 Engine/安装、Web 未验收。本阶段边界及后续限制见 ADR-121，不标记完整计划完成。

### Window/Input 归属与 native_window_info 复核

本轮实施：Window 配置、服务与原生窗口信息迁往 platform/window；Input 基础类型位于
core/input/types.hpp，动作状态独立到 function/input/action.hpp，服务归 function/input/service。
动作映射保持 resource/input，类型统一进入 gneiss::input；窗口类型进入 gneiss::window。
资产 GUID、文件格式与输入行为不变，不为目录新建链接目标。

本节迁移前，native_window_info 混合原生句柄、尺寸和 needs_recreate。源码链路为窗口 resize/scale
置位，Application 成功提交帧后清位，Render 回执汇总后重试，后端 acquire/present 也可置位；
capture_frame 还复制该结构并改写尺寸。当前协议本轮不修改。

本节最初提出的拆分建议（随后经用户确认，实施记录见下）：
- 原生句柄是窗口生命周期内的借用，仅用于渲染初始化/表面重建，不每帧作为可写窗口状态传递。
- 窗口尺寸和表面变化代次由 Window 单独维护，对外只读；同尺寸的缩放/表面变化也能推进代次。
- Render 持有交换链实际尺寸、已采用代次和重试状态；仅成功重建后采用代次，拒绝/丢帧不丢失变化。
- 捕获/离屏目标尺寸属于视图请求，不伪造窗口尺寸。移除可写 native_window() 前迁移这些消费者。
- 验收连续 resize、最小化零尺寸、背压/替换帧、GPU out-of-date 重试及离屏捕获。
  像素尺寸与窗口坐标尺寸的转换须核对 Granit 契约，不把名称变化等同于行为完成。

本轮验证：Windows Clang Debug 全量构建通过；14 项输入、动作映射、默认注册表、边界、
窗口/Application 与安装 Runtime 测试通过。异步启动与停止测试原先匹配旧 stage 日志，
已更新为当前协议；启动同步改读实时 stdout，退出后仍检查文件日志中的停止与关闭记录。
静态完整构建及 Web 本轮未验证；当时 native_window_info 拆分仅为建议；本轮实施见下。


### 2026-11-17：窗口表面快照与后端重建状态

已确认边界见 [ADR-118](../decisions/ADR-118-persistent-render-scene.md)。

- [x] 原生句柄只用于初始化；Window 返回值快照，删除可写 native_window()。
- [x] 通过 Granit get_state 的 framebuffer 字段统一像素尺寸；resize/scale 推进 revision。
- [x] 帧请求仅携带 surface；Backend 独占重建重试状态，删除 Service/Application 旧标志链。
- [x] 捕获请求显式指定输出尺寸，窗口零尺寸保留离屏执行路径。
- [x] 验证队列替换保留表面代次、连续 resize、最小化/恢复、不同尺寸捕获与正常窗口呈现。
- [x] 完成相关构建、测试及本地提交。

不承诺模拟所有驱动错误：真实 resize 路径用于集成验收，GPU out_of_date/not_ready 分支需结合
代码检查；没有稳定故障注入时不把它们宣称为已逐项触发。静态/Web 另行验收。

本轮验证：Windows Clang Debug 全量构建通过，8 项相关测试通过（包含 render_executor、
render_frame_packet、render_scene、ui_draw_list、真实窗口 smoke、原生应用流程、原生窗口资产
集成及安装 Runtime smoke）。真实窗口 smoke 新增连续 resize、最小化时离屏捕获、恢复呈现、
捕获尺寸不修改窗口尺寸的断言；执行器测试确认替换后的帧保留最新表面 revision。


### 2026-11-17：Granit 0.71 与原生句柄联合体

- [x] 核对上游最新标签及 HEAD，固定 0.71.0 提交和 SHA-256；同步 PACKAGE/静态安装最低版本。
- [x] 用 backend + union data 表达平台身份，统一查询并显式转换，不泄漏第三方类型。
- [x] 适配窗口标志枚举与 Canvas Surface；SDL3 表面需单独接入 IntegrationSDL3，当前返回 unsupported。
- [x] 完成共享完整构建、静态相关目标构建、相关测试及安装消费验证，记录未验证平台。

验收详情见 [Granit 0.71 升级记录](../records/2026-11-17-granit-0.71-upgrade.md)。

原生句柄变化不是尺寸变化：收到 native_handle_changed 时当前返回 unsupported，不使用失效表面。
本轮不启用 SDL3 窗口后端，不承诺 Gneiss Web 全链路可用，不修改 Granit 上游。

### 2026-11-17：Window/Input 调用链收敛

边界以 [ADR-122](../decisions/ADR-122-window-input-boundaries.md) 为准。

- [x] 保留平台采集与输入语义两个服务，统一为一次采集调用，删除快照搬运与公开逐事件轮询。
- [x] 引入 Core window::id，转换时保留来源，窗口重建不复用身份。
- [x] Input 删除状态 setter，动态事件缓冲替代固定容量；测试改为验证突发输入完整交付。
- [x] 验证动作边沿、窗口身份生命周期、原生窗口流程及受影响构建。

Windows Clang Debug 完整构建通过；7 项相关测试通过，包括 input_service、action_map、
cpp_input、granit_platform_smoke、native-application-flow、assets.application-native-window
及 runtime.installed-smoke。本轮未验证静态构建、Linux 或 Web。

### Files 后端封装与初始化（已完成桌面迁移）

- 同步工厂仅构造并保存配置，外层只引用 backend 与配置；具体类隐藏在 cpp。
- 先配置挂载，再由 service.initialize 去重初始化所有后端；冻结挂载。
- 后端初始化允许内联或异步完成，服务通知统一由 dispatch_completions 分发。
- 初始化成功前拒绝 I/O；失败等待所有已接受初始化收尾后通知，不自动重试。
- 初始化期间 shutdown 等待已接受工作收尾，先通知初始化取消，再通知关闭；析构可放弃通知。
- 一个后端由一个服务负责初始化与驱动，跨服务共享的测试改为独立后端或明确的 source_domain。
- 迁移 file_* 文件名、私有路径工具、工厂头与实现，更新 CMake/JS 路径及所有消费者。
- 验证去重、空挂载、内联/延迟完成、失败、关闭竞态、挂载回退、读写和 Application 启动。
- Web 只迁移既有后端，不扩展应用导出；没有 Emscripten 环境时明确未验证。


本节实现与桌面验收已完成：共享完整构建、17 项相关测试，以及静态 Engine/文件测试目标构建和
2 项文件测试通过。迁移使用同步工厂与服务级完成通知，未增加公共后端状态查询。
Web 后端及测试源码、CMake/JS 路径已迁移，但本机没有 Emscripten，编译与浏览器验收未执行；
不宣称完整 Web 支持。详细结果见 [Files 收敛记录](../records/2026-11-17-files-backend-initialization.md)。


### Platform I/O 职责收敛

已确认范围：`io::executor` 独立拥有 I/O 线程和任务队列；Files、IPC 与编辑器文件监听器
按各自生命周期使用执行器。文件操作属于 Files，不增加新的 I/O 服务或后端抽象。

- 删除没有生产消费者的 `uv_async_file`、`uv_file_reader`、`uv_path_resolver` 及其专用测试。
- 执行器入口为 `platform/io/executor.hpp`，libuv 访问与错误转换放在 `src/platform/io/detail`。
- 目标改为 `gneiss_io`，libuv 私有链接，不传播实现目录；保留隔离 libuv 的 PImpl。
- 保留执行器排队、停止、压力及异常边界测试，验证 Files、IPC 与编辑器文件监听消费者。
- 不改变线程所有权、队满返回、关闭排空语义；不保留旧类型与头文件兼容别名。

实施状态：已完成。Windows Clang Debug 共享构建及 19 项相关测试通过；静态 Engine、
执行器、IPC、原生文件目标构建及 3 项相关测试通过。未执行 Linux/Web 验证。


### Logging 文件与输出边界整理

- 输出工厂迁到 include 的 `core/logging/sinks/`，实现合为 `sinks.cpp`；目标改为
  `gneiss_logging_sinks`，停止公开传播实现目录。
- 服务、频道句柄、连接、默认路由按对象拆分实现；共享控制块保留在 detail，维护弱引用失效语义。
- Logger 统一持有 string_name，消除局部字符数组借用；默认服务、动态 attach 与队列满策略不变。
- 生命周期契约违例改为断言；保留文件输出可恢复错误与配置参数检查。
- 验证频道生命周期、默认绑定、服务关闭、多线程投递、队满、输出失败和宿主日志接入。

实施状态：已完成。Windows Clang Debug 共享构建及 17 项日志、宿主、头文件边界测试通过；
静态 Engine 构建及 3 项日志测试通过。未执行 Linux/Web 验证。
