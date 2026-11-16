<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-119：Engine 头文件、构建目标与测试组织整理

## 状态与范围

2026-11-16：实施中，P1 最小构建闭环已通过。承接用户根目录 engine 迁移，
已恢复离线配置、Engine/minimal/World/反射边界目标编译；4 项定向回归通过。
其余宿主、工具、私有测试及静态/安装配置尚未验收，不将当前结构视为最终边界。

目标：明确 engine/include 与 engine/src 的边界，消除 CMake 旧路径和重复维护，
使目标依赖与代码所有权一致，降低测试对实现布局的耦合。
非目标：重新设计 World/Render/资产协议、恢复稳定 C++ SDK、全仓改命名空间、引入测试框架、
为目录各建一个库、修改第三方仓库。本计划暂时优先于 PLAN-118 的接口继续迁移。
此前 PLAN-118 查询与线程测试修改已随构建恢复验证，文档状态已在 8a570c1d 同步。

## 当前核查事实

按本轮工作区静态扫描，engine/include 有 173 个 .h/.hpp，engine/src 有 48 个头文件、
107 个 .cpp；tests 有 213 个 .cpp。这些是文件数，不是有效 CTest 数或质量指标。

- engine/include/gneiss/engine 已形成对外包含根，但仍混有 resource_cache、main_loop_wakeup、
  serialization/detail/json_adapter 等实现。resource_cache.hpp 相对包含 detail/asset_entry.hpp，
  暴露目录与实现目录拆开后该包含关系需修复，不能靠添加所有私有搜索路径掩盖。
- meta/registration.hpp 直接包含 meta/detail/type_ops.hpp；serialization/detail/typed_decode.hpp
  包含模板实现。不能仅依据 detail 名称认定它们应从 include 删除。
- engine/CMakeLists.txt 仍列 engine/core/... 等旧相对源路径，版本头输入仍指向 src/engine；
  私有包含根仍有 PROJECT_SOURCE_DIR/src。原 src/CMakeLists.txt 已基本清空。
- cmake/internal_interfaces.cmake 部分清单已改成 engine/include，但 core/tasks 等仍引用旧路径。
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
tests/
  CMakeLists.txt
  core/ meta/ serialization/ asset/ scene/ render/
  host/ tooling/ support/
```

消费者只链接对应目标并包含 <gneiss/engine/function/scene/world.hpp> 等入口；
不手动添加 engine/src，不复制契约头到构建树。Engine 自身用 PRIVATE engine/src，
用 PUBLIC 的 BUILD_INTERFACE 暴露 engine/include；安装接口另行明确，不自动安装该目录。
初始化仍调用既有初始化/创建入口，正常使用保持原 API，业务失败仍由既有 result 表达，
关闭仍按现有所有者顺序执行；本计划不改变资源保活和销毁语义。

## CMake 目标与职责

| 层次 | 目标处理 |
| --- | --- |
| 引擎入口 | 保留 gneiss_engine / gneiss::engine；统一管理自身源清单、生成版本头、导出定义与依赖 |
| 仅头契约 | 合并 value/native/result 等复制清单目标；有真实独立消费者的头目标可保留，但直接引用源 include，不复制 |
| 已有独立能力 | tasks、events、IPC、I/O、process、dynamic_library 按实际独立消费保留；不是每个命名空间都建库 |
| 日志后端 | 文件输出实现与 logging 服务契约分开；spdlog 只属于实际输出实现，不让所有消费者链接它 |
| 平台/依赖 | Granit、libuv、libassert、GLM 等由依赖脚本提供目标，各实现目标声明 PRIVATE/PUBLIC，不全局传播 |
| 宿主和工具 | root 只装配 engine、Editor/Runtime/Tooling、示例和测试；src 保留现有宿主代码，不借此次任务整体改名 |
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
- [ ] P2：完成 include/src 分界和所有实际消费者迁移；移除重复头复制清单，检查模板包含闭包与第三方传播。
- [ ] P3：收敛 CMake 目标归属、重复编译和脚本目录；Shared/Static 分别检查导出和链接，消除双份全局状态。
- [ ] P4：按领域整理测试目标与辅助；先迁移必要覆盖再删除冗余，实现准确标签与最小测试入口。
- [ ] P5：同步 coding-style/building/架构与边界文档，定向回归后检查旧路径、旧复制清单、旧测试组成退出。

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
  Tooling 的独立契约复制清单仍保留，P2/P3 再处理，不宣称复制机制全部删除。
- 旧 core-boundary 等路径扫描规则仍需迁移；本轮未运行它们，不用空扫描声称边界验收通过。
- 其余目标的私有头访问、模板闭包、重复实现与共享/静态安装验收留待 P2/P3。
- 本轮纳入用户已有目录迁移及必要包含修复；没有回退用户的相机数据结构调整。

## 决策边界

已采用：根 engine 延续、include/src 按消费者分界、减少复制清单和重复实现、测试按行为价值清理。
本计划未授权改变稳定 ABI、安装全部 C++ 头或新增 Core 独立运行时所有者。
若实施发现必须拆出独立二进制 Core 才能解决真实多消费者依赖，先给出链接与单例归属方案并更新 ADR；
否则优先保留现有所有权。不需要用户再次为普通文件移动、源清单修复或测试去重逐项确认。
