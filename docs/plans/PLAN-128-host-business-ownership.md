<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-128：Editor、Runtime 与项目宿主业务归属

## 状态

2026-11-22：实施中。P1 代码迁移与本地定向验收完成，手工高 DPI/连续交互待验收；P2 代码迁移与本地自动化范围已完成，P3 正在实施，Runtime 会话所有权与构建边界已提取；P4 待实施。
证据见 [P1 验收记录](../records/2026-11-22-editor-viewport-extraction.md)及
[P2 验收记录](../records/2026-11-22-editor-session-extraction.md)。
属于 [0.52 架构整理](VER-052-0.52.0-architecture-cleanup.md)，不包含远端操作或发布。
长期边界见 [ADR-131](../decisions/ADR-131-host-business-ownership.md)。

## 背景与目标

当前源码已具备独立 Editor/Runtime 进程、Application 阶段调度、World 自有系统、
文档到 World 的投影和持久渲染场景。问题主要在宿主内部归属与构建边界：

- editor/application.cpp 同时装配服务并维护相机、目标、Gizmo、拾取、文档和关闭交互。
- gneiss_runtime_ipc 实际还编译场景加载、资产重载及运行检查业务。
- gneiss_scene_document 包含 runtime_launch、Gizmo 拖动及拾取协调，名称与职责不完全一致。
- 项目选择界面在 editor/project，根 project 是共享配置库，gneiss_project 是项目 CLI。

目标是让入口表达装配和调度，让业务对象拥有唯一状态，通过完整使用链路移除旧路径。
不以类数量、文件长度或静态库数量作为完成标准。

## 与已有计划的关系

- [PLAN-126](PLAN-126-application-scheduling.md) 的 Application 生命周期与阶段保持不变；不重开已完成任务。
- [PLAN-127](PLAN-127-world-system-scheduling.md) 的 World 系统与显式宿主调度保持不变。
- [PLAN-115](PLAN-115-persistent-render-scene.md) 继续负责渲染能力与 GPU 验收；本计划只迁移消费者归属。
- [PLAN-119](PLAN-119-engine-build-test-layout.md) 的 target-based CMake 和测试组织规则继续适用。
- 跨计划进度只汇总到 [DEV-052](DEV-052-architecture-cleanup.md#收尾审计后的明确待办)。

## 非目标

不新增调度框架、通用 Context、服务定位器、自动依赖注入或强制 system 基类。
不增加多文档管理器、跨 World 移动、IPC 消息能力或新的渲染后端功能。
不改变文档权威性、撤销重做语义、资源引用所有权及 World → Render 单向命令同步。
不把项目选择、Editor 和 Runtime 改成一个长期共享服务实例的超级宿主。

## 已确认的所有权

| 对象 | 唯一拥有的状态 | 借用与责任边界 |
| --- | --- | --- |
| app::application | 应用级服务、生命周期和阶段调度 | 不自动创建或遍历 World |
| Editor 会话 | world_owner、渲染绑定、document_editing、场景加载操作、scene_viewport | 借用 Application 与项目工作区；协调共同生命周期 |
| document_editing | 编辑文档、历史及编辑操作 | 保留现有编辑职责，不拥有窗口、Render 或 World |
| scene_projection | 文档对象到运行对象的映射及投影状态 | 借用会话 World，不成为文档或实体的第二所有者 |
| scene_viewport | 编辑器相机、目标、Gizmo/拾取交互状态 | 借用编辑会话能力和 Render；不拥有文档、World 或历史 |
| Runtime 会话 | 游戏 World、渲染绑定、加载操作和运行状态 | 借用应用服务，不拥有 Editor 文档 |
| Runtime 连接 | 子进程、IPC 生命周期及远端检查镜像 | 不拥有或直接修改本地编辑文档 |
| project_workspace | 项目配置、资产监控、导入与构建任务 | 不负责窗口循环、文档历史或渲染帧 |

Editor 会话是共同生命周期的协调者，不复制文档、加载器或连接对象已有状态。
UI 通过业务入口发起操作并读取状态；不再维护对应成功、失败、暂停等第二套标记。
优先迁移已有对象；只有真实共同生命周期或独立业务职责才引入新对象。

## 目标 API 与使用闭环

以下为目标契约示意，不是当前可编译接口；具体签名在各阶段迁移真实消费者时确定。

```cpp
namespace gneiss::editor {
class session {
public:
    session(app::application&, project_workspace&); // 仅装配，不开始异步工作
    result<void> open_scene(std::string_view uri);   // 接受操作，不代表加载完成
    void update(const frame_time&);
    result<void> synchronize();
    void append_views(render::frame_request&) const;
    document_editing& editing() noexcept;
    scene_viewport& viewport() noexcept;
};
}
```

- open_scene 返回可恢复的请求错误；加载完成/失败仍由单一加载操作状态表达，UI 读取。
- 新请求替换/拒绝在途请求、加载失败是否保留旧文档，迁移前核对现有契约并保持行为；不暗改。
- update 处理会话业务；不重复推进平台事件、文件服务或应用级完成队列。
- synchronize 在 World 变更发布后同步渲染绑定，再处理依赖该同步的视口请求。
- append_views 只汇总当前视图；宿主合并 UI 后统一提交帧，不在每个面板单独提交。
- scene_viewport 提供 resize、相机输入、select_at、取消交互和当前视图能力；
  图像布局/鼠标采集留在 UI，坐标转换只有一处实现，Gizmo 与拾取优先级不分散。
- 通用 session 构造不访问尚未初始化的服务；启动和可等待关闭接入 on_start/on_stop。
- UI 操作失败显示在对应业务状态中；不把可恢复编辑错误直接传播为 Application 退出。
- Runtime 会话提供打开场景、暂停、更新、同步与视图汇总；IPC 转换命令并调用业务入口。
  暂停只停止游戏模拟，不停止通信、窗口事件、资源完成通知和必要渲染。

完整使用链路：宿主读取配置 → 创建 Application 和业务对象 → 注册系统 → 服务启动 →
打开初始场景 → 编辑/运行/显示 → 处理请求失败或用户关闭 → 等待会话任务退出 →
释放会话资源 → 关闭应用服务。

关闭项目时先完成保存确认；用户取消则继续当前会话。确认关闭后停止接收新业务请求，
取消或排空加载/导入/拾取与 Runtime 工作，再释放视口、渲染绑定、投影与 World。
具体析构顺序须确保所有借用先结束，后台回调不再访问会话；Application 服务最后关闭。
保持现有旧 Editor 会话完全结束后重新创建项目选择窗口的流程。

## 目标目录与构建边界

```text
editor/
  main.cpp, application.*       # 参数、装配、生命周期
  session.*                    # 编辑会话
  documents/                   # 文档、投影、历史、编辑命令
  viewport/                    # 相机、Gizmo、拾取协调
  ui/                          # ImGui 面板与输入采集
  runtime/                     # 子进程连接、IPC 适配、远端检查镜像
  project/                     # 工作区、项目选择界面、CLI
  assets/                      # 导入、监控、资产浏览模型
runtime/
  main.cpp, application.*
  session.*                    # 游戏运行会话
  scene/, assets/, inspection/, ipc/
project/                       # 共享项目配置
protocol/                      # 消息模型与编解码
tooling/                      # 离线导入与构建
```

目录表示业务归属，不要求每目录一个库。不要只为达到上述布局创建转发头文件。
Editor 与 Runtime 业务分别归 gneiss::editor、gneiss::runtime；宿主入口可保留 gneiss::host。
共享项目类型的命名空间随实际消费者审查后确定，不与 CLI/项目选择 UI 混为一谈。

构建目标按真实消费者收敛：

- 文档目标只保留文档、编辑操作、历史与投影，不依赖 ImGui、IPC 或子进程。
- 视口交互归 Editor 业务/UI 组合；不为了测试把它重新放回文档库，也不复制生产源码编译。
- Runtime 场景/资产业务从 gneiss_runtime_ipc 移出；目标可整理为 gneiss_runtime_host。
- runtime_launch 从文档目标移入 Runtime 启动业务的拥有者；连接与远端镜像按使用链路整理。
- 候选目标名为 gneiss_editor_documents、gneiss_editor_runtime、gneiss_runtime_host；
  是否独立成库取决于链接消费者。gneiss_editor_ui/assets 与共享库按实际职责保留。
- 不增加空的 interface 库或旧目标别名来永久维持双轨；同步迁移测试、工具与安装依赖。

## 实施顺序与退出条件

- [x] P1：提取 scene_viewport（代码迁移与本地自动化范围）。迁移相机、目标、Gizmo、拾取状态；UI 保留布局和输入采集。
  editor_state 已移除对应字段；同步 system 在 binding 成功后处理拾取，present 只汇总视图。
  相机、缩放、Gizmo、选择及关闭目标释放定向验证通过；手工高 DPI/连续交互仍待补。
  新视口与拾取、Gizmo 拖动实现复用 gneiss_editor_ui，已移出文档目标；不新增仅为目录服务的库。
- [x] P2：建立 Editor 会话（代码迁移与本地自动化范围）。迁移 World、绑定、加载协调，明确投影与文档的借用关系。
  会话生命周期、打开/加载、同步与视图汇总已移入 editor::session，入口负责装配和阶段注册。
  打开、失败保留、取消、关闭重开及既有编辑/保存测试通过；完整 GUI 保存确认/取消与连续重开仍待手工验收。
  会话只借用 Application；当前没有工作区直接调用，不增加未使用的 project_workspace 构造依赖。
  会话暂由既有 gneiss_editor_ui 生产目标编译，P4 再按实际消费者整理目标名；不向文档库引入 UI 依赖。
- [ ] P3：建立 Runtime 会话，分离 IPC 与业务。保留显式请求和完成状态，不新增协议。
  runtime::session 已拥有 World、渲染绑定和加载器；同步与视图汇总由会话执行。
  场景、资产重载和检查实现已移入 gneiss_runtime_host，gneiss_runtime_ipc 仅保留通信处理。
  loading() 暂向宿主提供借用：协议适配、重载回调和检查状态仍在入口，迁入会话业务接口后删除该过渡入口。
  暂停/恢复命令的实际业务消费仍需核对并接入，不能以 IPC 命令测试代替运行行为验收。
  本阶段记录见 [Runtime 会话所有权验收](../records/2026-11-22-runtime-session-ownership.md)。
  退出条件：独立运行及 Editor 启动、暂停、恢复、停止都通过；IPC 不拥有游戏 World。
- [ ] P4：整理工作区、目录、命名空间和 CMake 目标。同步迁移测试与文档。
  退出条件：删除旧入口及重复状态；目标与实际职责一致，无新增循环依赖。

每组先给出真实调用样例及初始化、失败、关闭路径，再实现；同一特性分支持续本地提交。
出现跨阶段过渡时注明借用关系、消除条件，不把过渡状态写成最终架构。

## 测试与验收

遵循 [测试维护指南](../guides/testing.md)，复用并迁移已有测试，不按新类数量增加测试。
所有新增/迁移测试目标（含夹具库）设置 FOLDER "Tests"；只保留有真实行为价值的检查。

| 阶段 | 主要验收 |
| --- | --- |
| P1 | 相机、Gizmo、GPU 命中/背景、旧点击失效、缩放与视口关闭；手工高 DPI 交互与自动测试分开记录 |
| P2 | 场景加载成功/失败/取消、撤销重做、关闭取消、保存后关闭、项目重开；旧异步结果不能污染新会话 |
| P3 | 无 Editor 独立运行、IPC 连接与断开、暂停恢复、场景操作失败、停止时在途任务清理 |
| P4 | Editor/Runtime/项目 CLI/项目选择界面构建与冒烟；依赖闭包和受影响共享/静态链接验证 |

最终按“打开项目 → 编辑并撤销 → 视口选择 → 启动/暂停/停止 Runtime → 关闭项目 → 重开”验收。
关闭后不得遗留会话回调、子进程或资源；GPU 完整故障矩阵仍属于 PLAN-115。
构建命令以 [构建指南](../guides/building.md) 为准；未运行的平台和手工项如实记录。

## 风险与实施前检查

- scene_viewport 是否直接借用编辑会话或仅借用 document_editing/绑定，在 P1 用真实调用链决定；禁止形成相互拥有。
- result 仅用于可恢复失败；纯查询和内部前置条件不机械包装 error/result。
- resize 背压、旧目标保留、取消只放弃结果等现有语义必须保持。
- 迁移前确认 Runtime 无 Editor 运行与游戏模块生命周期，避免让业务隐式依赖 IPC。
- 不以抽取类后头文件更短作为验收；检查调用方是否仍需协调重复状态或隐式时序。
