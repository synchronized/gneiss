<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-126：Application 生命周期、调度与宿主状态组合

状态：收尾验收中。生命周期、具名系统与宿主启动等待已接入；实际工程 A → 项目管理器 → 工程 B 已验证。仍需最终旧路径审计和验收归档；下方早期目标 API 为设计草案，当前接口以 Reference 为准。
架构取舍见 [ADR-129](../decisions/ADR-129-application-scheduling.md)。本计划属于
[0.52 架构整理](VER-052-0.52.0-architecture-cleanup.md)，不包含远端发布授权。

## 当前问题与目标

当前 `engine/include/gneiss/engine/function/application/application.hpp` 与对应
`engine/src/function/application/application.cpp` 同时维护服务装配、唯一 World、渲染视图、
Action Map 加载、环境文件读取、资产批次及主循环。`create()` 后还需安装任务执行器；
`run(1)` 返回时会执行帧收尾，不能直接作为外部事件循环的一步。Editor、Runtime、项目管理器
分别协调分步关闭。以上为实施前问题；迁移现状见下方进度表。

目标是通过共享基础循环与宿主系统组合支持三种真实用例，不建立另一套 ECS 或业务对象所有权。

## 已确认边界

- Application 唯一拥有应用级服务、调度表、转换队列与生命周期状态。默认不创建 World。
- Session 拥有 World、视口、渲染绑定、资产请求和业务数据；World 仍是场景实体唯一所有者。
- World → Render 同步留在 World 一侧，以命令批次提交，不恢复 Application 的场景快照组装。
- system 是函数或可调用项，不强制继承；安装函数组合服务、系统和状态，不实现动态插件。
- 函数捕获为借用时必须覆盖整个注册与关闭过程。系统不是独立业务状态的默认所有者。
- 服务访问保持显式；不新增通用服务定位器、任意类型 Context 或自动参数注入框架。
- 第一版串行执行、固定调度阶段、构建期完成注册；不做自动并行与运行期任意修改系统图。
- 配置中跨异步启动使用的字符串必须拥有存储。业务资源 URI 不加入 Application 基础配置。

## 状态、调度与所有权

Application 生命周期固定为 `starting → running → stopping → stopped`；启动失败也经收尾进入 stopped。
宿主定义独立的状态类型，例如 Runtime 的 loading/playing/paused/load_failed，Editor 的
selecting_project/opening_project/editing/closing_project。服务就绪后的 loading 属于 running 内的业务状态。
暂停模拟不能停止窗口、完成队列、编辑器 UI 或渲染。

每轮固定顺序：平台 events → completions → 状态转换 → input → update → synchronize → present。
starting/stopping 只执行相应生命周期步骤及必要事件、完成分发，不执行普通业务系统。
`on_update(state)` 是 update 阶段的状态条件，不是另一套循环；基础分发不受业务状态限制。
同一阶段按注册顺序执行；有顺序依赖的操作显式组合为 sequence，不按类型名或隐式发现决定顺序。

每种业务状态类型有独立当前值；最初不提供嵌套状态机。状态转换只在主线程边界提交：

- completions 中的请求可在本轮转换阶段生效，update 中请求在下一轮生效。
- OnExit(old) → 更新状态 → OnEnter(new)，初始状态的 OnEnter 在服务就绪后执行一次。
- Enter/Exit 中的新请求留到下一轮，不递归转换，不修改正在遍历的系统列表。
- 同轮同目标请求合并，不同目标报告冲突，禁止最后写入者静默覆盖；应用退出优先。
- 状态进入不是事务。失败不保证自动恢复副作用，资源由明确所有者和成功初始化记录清理。

## 目标 API 与完整使用链路

下列接口是草案，最终命名与最小实现一起评审；不提前建立兼容别名。

```cpp
runtime_session game;
application app; // 后创建先销毁；game 覆盖注册回调和关闭过程
install_platform(app, platform_options);
install_assets(app, asset_options);
install_render(app, render_options);
install_game(app, game);
auto result = app.run(); // 包含启动、循环与完成关闭；失败也必须清理已建立资源
return result ? 0 : 1;
```

安装函数只是注册组合，不执行隐藏的业务加载：

```cpp
app.init_state(game_state::loading);
app.on_enter(game_state::loading).add([&game] { game.begin_loading(); });
app.on_update(game_state::loading).add([&game](const frame_time& t) { game.draw_loading(t); });
app.on_update(game_state::playing).add([&game](const frame_time& t) { game.update(t); });
app.on_exit(game_state::loading).add([&game] { game.clear_loading_ui(); });
app.add_system(stage::synchronize, [&game] { game.synchronize_render(); });
app.add_system(stage::present, [&game] { game.present(); });
```

system 支持 void 或 result<void>；无实际可恢复失败的操作不包装成功值。调度错误应附带阶段和
系统身份并终止普通业务执行，进入受控关闭；加载失败显示重试界面属于业务状态，不必终止应用。
调用线程、重入等程序契约按现有断言规范处理；意外异常按既有异常策略清理后传播，不逐系统吞掉。

外部事件循环调用同一 `step()`，直到 stopped；`run()` 只是同一路径的循环驱动。
`request_exit()` 请求关闭，后续 step 执行收尾。单步返回不得隐式等待全部 GPU 帧。
桌面析构保底与外部异步宿主的停止前置条件在阶段 A126-01 定稿，禁止析构后仍执行借用回调。

## 异步启动、加载和关闭

启动步骤表达完成、等待完成凭据或失败；同步步骤只运行一次。等待期间继续处理必要事件与
完成通知，不能每帧重跑初始化，也不能让宿主手动 poll 每个服务。仅记录成功建立的步骤以清理。
文件后端未就绪时不得进入业务 OnEnter。任务执行器在装配阶段确定，不保留创建后的半初始化 attach。

业务加载由 Session 持有请求：OnEnter 提交一次，完成后经主线程消息请求下一状态。多依赖使用
已有批次请求；不新增 Application 资源记录。流程取消或退出时撤销请求并使本轮代次失效，迟到
回调不能启动旧状态。转换请求端不保活 Application，底层异步完成不能直接跨线程修改状态。

关闭顺序：拒绝新业务操作 → 停止普通系统 → 宿主释放 World、绑定并取消会话任务 → 排空相关
任务与完成消息 → 释放资产、文件、渲染等服务 → stopped。具体服务顺序按真实依赖复核，资源
仍被 GPU/任务引用时不得提前释放。并非简单逆序执行所有 shutdown 回调。
OnExit 不能代替应用关闭通知；业务回调解除和异步工作排空都完成后才允许销毁 Session。

## 旧接口去向与退出条件

| 当前职责或入口 | 目标 |
| --- | --- |
| native_world、默认 World、主相机选择 | 宿主 Session；无 World 的项目管理器不承担场景状态 |
| set_views、set_view_camera、set_view_target | 宿主视口状态和 Render 帧提交 |
| capture_frame、render_statistics | render::service |
| load_assets、批次状态 | asset_manager 与调用者持有的请求 |
| begin/action_map_status/cancel_action_map | Session 加载请求，成功后应用到 input::service |
| find_action、get_action_state、输入与窗口查询 | Input/Window 原生入口 |
| 环境文件读取与 pending 状态 | Asset 加载、宿主选择环境、Render 应用 |
| report | 日志/诊断系统 |
| set_paused | Session 模拟时钟 |
| attach_task_executor | 启动装配配置 |
| request_shutdown/advance_shutdown/shutdown 的宿主协调 | request_exit + 同一 run/step 生命周期 |

迁移按完整消费者推进，不能以转发方法保留所有旧入口。明确短期适配存在的位置、删除条件；
禁止新旧循环同时推进同一个服务或维护两份加载、关闭状态。审计现有 detail/service_registry 的
实际消费者，不因名称相近直接扩展成新调度器；证实无生产用途后再删除旧实现与专属测试。

## 文件与构建范围

优先在现有 Function/Application 领域内形成 application.hpp/.cpp、schedule.hpp/.cpp、state.hpp。
头文件进入 engine/include 对应目录，实现进入 engine/src；不为了目录整齐增加库目标。
状态类型与 install_game/install_editor 等业务组合位于 Runtime/Editor 自己的模块。
`app::stage` 等命名空间仅为草案，不顺带重命名整个 Runtime 或引入第二个 Application 门面。

## 实施顺序与退出条件

- [ ] A126-01：审计三个宿主、示例、测试及服务依赖；定稿 API、停止失败与借用生命周期规则。
  为成功启动、异步等待、失败、请求退出分别给出可编译最小用例；补充旧入口删除清单。
- [ ] A126-02：实现最小串行调度、状态转换与共享 run/step 生命周期；先接入无 World 的项目选择界面。
  验证真实窗口/UI、初始化失败与关闭；单独调度测试通过不算本阶段闭环。
- [ ] A126-03：迁移 Runtime 与 minimal/scene_viewer 示例。Session 接管 World、加载、暂停与渲染绑定；
  完成请求驱动状态转换，移除相应 Application 专用接口。
- [ ] A126-04：迁移 Editor。文档、预览、IPC 和视口使用同一调度基础；移除手动关闭推进与重复转发。
  验证多 World 使用和项目关闭后再次打开，保持撤销重做与文档权威边界。
- [ ] A126-05：删除全部旧路径、无用配置/状态/测试；更新当前 Reference、示例和构建依赖。
  审核最终调用复杂度、失败收尾与跨宿主测试，不以对象数量减少作为验收。

## 验证策略

遵循[测试维护指南](../guides/testing.md)：复用宿主现有行为测试，迁移或删除绑定旧调度机制的断言，
不为每个注册方法增加重复测试。新增测试目标和夹具统一 FOLDER "Tests"。

- 顺序调度、Enter/Exit 恰好一次、延迟转换、重复请求合并、冲突与退出优先。
- 初始化步骤不重复，部分成功后失败只清理已建立资源；正常等待不作为失败。
- 取消及迟到完成不切换旧流程，关闭时继续分发完成，不产生悬空捕获或双重销毁。
- run 与 step 使用相同阶段语义；暂停只停止模拟；无 World 宿主与多 World 宿主均可运行。
- 三个真实宿主及示例构建，相关 CPU 回归；真实窗口/双视图/关闭用例按受影响范围验证。
- 改变导出和依赖传播时验证共享/静态构建与头文件闭包；不恢复已撤销 C ABI 或安装 SDK。
- 无法执行的 GPU、平台与交互验收明确记录，不以 CPU 通过代替。

## 已定稿的实现细节

1. 启动/停止使用 start_progress/stop_progress 的 pending/complete；宿主持有实际请求，
   completions 和服务完成分发继续运行，不增加另一套 future。同步步骤完成后不重放。
2. 运行错误优先于停止错误；停止步骤失败仍执行后续清理。step 持续驱动至终态；桌面析构
   走同一清理步骤并同步等待。意外异常传播，致命终止不承诺完整清理。
3. system 使用 Application 内唯一名称、注册顺序；失败日志含阶段、名称、原始错误码。
   顺序相关工作可注册为一个组合 callable，不新增通用依赖图或 sequence 对象。
4. Application 拥有应用服务，配置借用 executor；宿主必须使执行器存活至 Application 关闭。
   协作执行器通过 completions 执行 ready 工作；不增加另一套共享执行器所有权。
5. 无待处理转换时，同状态请求为无操作；不同目标的待处理请求返回冲突且保留首次请求。
   重新加载由业务操作发起，不隐式重复 Enter/Exit。

对应可执行用例在 tests/engine/application/native_application_flow_test.cpp：startup_systems、
starting_completions、host_shutdown、complete_lifecycle、state_systems 和 stopping_completions。
日志阶段及错误身份由 application_log_test.cpp 验证。

## 当前进度与剩余工作

| 范围 | 已实现 | 尚未完成 |
| --- | --- | --- |
| 调度与状态 | 固定阶段、枚举状态、延迟转换、冲突拒绝、具名错误；类型统一到 app 命名空间 | 最终旧入口审计 |
| 生命周期 | run/step 共用推进；基础服务延迟到启动；启动 pending/complete；停止分发完成；旧 run(n)、公开 shutdown 和平台回调已删除 | 最终契约与实现逐项核对 |
| 所有权 | 宿主拥有 World/绑定/视图；多 World 文档隔离回归通过；真实关闭工程再开另一工程通过 | 汇总验收证据 |
| 项目管理器 | selecting 状态、UI 释放与异步任务排空进入停止系统；无隐式 World | 最终旧路径审计 |
| Runtime | IPC/停止文件在 completions；输入请求由 on_start 等待；资产/场景/报告分离；统一停止清理 | 最终旧路径审计；真实进程测试未单独证明输入 pending 瞬间的 IPC 退出 |
| Editor | 会话 on_start；取消/等待/释放三段停止；实际 A → Manager → B 会话切换通过 | 最终旧业务转发审计 |
| 示例 | minimal 自有 World；scene_viewer 一次提交输入/场景，启动等待输入；单/双视图验证通过 | 汇总验收证据 |

当前接口契约以 [Application Reference](../reference/application.md) 为准。上方阶段复选框待最终
逐项审计后勾选，不再以早期进度表判断剩余实现量。共享全量构建与既有 CPU/GPU 回归已执行；
最近改动另有针对性回归。静态引擎、Application 测试及头文件闭包已验证，不表示全部静态宿主均验收。
停止阶段保留文档用于卸载清理，但资产编辑入口与统一内容修改入口拒绝新请求。

### 已完成验证

- window() 接入后 Editor、scene_viewer 与 Application 流程测试构建通过；无窗口查询回归、
  双视图 smoke、窗口缩放/最小化/截图 smoke 通过。原生交互测试曾在首轮返回 invalid_state，
  加诊断后未复现；测试改为直接获取所属窗口句柄，最终代码通过 18 次缩放、3 次最小化恢复
  和 5 次合成 DPI 变化。该结果不宣称已证明早先偶发失败的根因或覆盖物理多屏切换。

- Editor 及五项文档/加载/资产编辑回归构建通过；独立 World 下打开文档、取消、错误保留、
  快照恢复与撤销重做通过。文档须先于借用的 Application 和 World 关闭。

- Runtime 加载器使用独立 World 验证加载、Prefab 重载、相机选择与卸载，默认 World 保持空；
  Runtime、加载器及检查器构建通过，场景加载/检查与 Scene/Prefab 结构重载四项回归通过。

- Windows Clang Debug 全量构建通过。213 项 CPU 回归首次 211 项通过；资产编辑关闭边界
  修复后回归通过。预制体编辑独跑通过（约 42 秒），原 45 秒上限缺少并行调度余量，
  防挂死超时改为 90 秒；两项失败用例重跑均通过，预制体编辑耗时约 30 秒。

- Windows Clang Debug：Runtime、Editor、两例及两项 Application 回归目标构建通过。
- native-application、native-application-flow、texture-load-application、minimal-example 回归通过。
- 项目选择器 smoke 退出 0，但 GPU 实际执行帧数为 0，仅作为生命周期证据。
- Editor editor_demo 普通/协作 smoke 退出 0，执行 11/10 个 GPU 帧，无资源关闭错误。
- Runtime editor_demo 普通/协作 smoke 退出 0，执行 5/4 个 GPU 帧，任务 submitted=completed=3、
  retained=0；runtime.scene-loader 回归通过。
- scene_viewer --smoke --dual-view 退出 0，覆盖两组目标尺寸和双相机图像差异回读。

尚未验证其他平台、完整浏览器宿主、多 World 视口与项目关闭后重开；这些随对应所有权迁移验收。

### 宿主 World 与呈现迁移

Runtime 和 Editor 已独立拥有 World 与 world_render_binding，在 synchronize 阶段同步，
在 present 阶段直接提交 render::frame_request。Editor 独立选择视口、相机与离屏目标，
UI 适配器返回借用绘制数据，由宿主复制到帧请求。项目管理器直接提交 UI 帧。
停止系统先关闭文档与场景，再释放绑定及 World，最后由 Application 关闭服务。

前期过渡（本批已删除）：Application 曾为未迁移示例和测试创建默认 World；注册 present 系统的宿主
不再触发旧默认呈现，避免一轮双重提交。这不是最终协议，退出条件是 scene_viewer 和相关
测试迁移完成，随后一并删除默认 World、默认呈现及业务转发接口。

本批验证：Runtime、Editor、原生窗口交互目标构建通过；Runtime 普通及协作 smoke、
Editor editor_demo smoke、原生窗口交互测试均退出 0。未以此代替双视图示例迁移验收。

scene_viewer 已自有 World、渲染绑定、双视图目标及 UI 帧数据；场景装配辅助显式接收 World。
截图在同步阶段之后直接向 Render 提交本帧请求，关闭时先释放目标，再卸载场景、释放绑定与 World。
Windows Clang Debug 构建、单视图 smoke 与双视图两组尺寸/图像差异回读均通过。
生产宿主迁移完成不等于旧默认 World 已删除：测试仍使用 native_world()，下一步迁移这些消费者。

### 默认 World 测试消费者迁移

文档打开辅助改为使用传入的 World，删除与 Application 默认 World 相等的限制。
组件编辑、Gizmo、Prefab 编辑、资产命令、文档保存夹具和 Runtime 结构重载测试显式拥有 World；
重载辅助从已打开场景取得所属 World，不再从 Application 推断。相机选择测试只初始化内置注册
并创建 World，删除 Application 相机转发相关断言，保留相机覆盖、非法参数、线程与销毁语义。

该批仍属于默认 World 删除的前置迁移；加载/渲染测试与 Application 自身测试尚未全部迁出。
不得保留测试专用默认 World 或新增应用所有者包装来延续旧接口。

本批 Windows Clang Debug 相关目标构建通过，组件/Gizmo/文档编辑、资产与 Prefab 编辑、
Runtime 检查/加载/启动、Scene/Prefab 结构重载、相机选择、保存及灯廊工作流回归通过。
相机测试首次因缺少内置注册而创建场景失败，补上显式 initialize_meta 后重跑通过。
任务响应基准仅完成编译，未执行性能测量；本批未验证其他平台或静态构建。

加载测试辅助 activate_scene/load_scene 现在显式接收 World，不再从 Application 选择装配目标。
场景实例、共享资产、视觉失败隔离、导入产物和保存重开测试迁移到自有 World，相关五项回归通过。
Application 测试删除“默认拥有 World、关闭后 native_world 为空”的旧契约断言，保留生命周期、
失败清理及异步停止验证。GPU 验收与测量程序暂时显式传入默认 World，需连同帧提交/截图一起迁移；
这些工具已完成签名适配并编译通过，本批不宣称已移除生产实现的默认 World。

### 默认 World 与默认呈现退出

像素验收、平台 smoke、加载测量程序及属性检查测试已改为显式拥有 World；GPU 消费者
注册同步和呈现系统，截图直接提交 Render 请求。测试辅助只借用 World/绑定，不持有第二套应用状态。
Application 已删除默认 World、渲染绑定、视图列表、绘制列表、截图转发和默认帧提交。
旧更新阶段绘制许可状态也随之删除，present 阶段计时直接包围宿主呈现系统。
视图校验归 Render，绘制输入校验归 draw_list，测试不再要求 Application 保存这两类业务数据。

本批仍不代表 PLAN-126 完成：旧有界 run/关闭入口、业务转发与启动协议仍需按后续阶段收敛。

本批验证：Windows Clang Debug 全量构建通过，213 项 CPU 回归全部通过。
删除默认实现后，像素验收、平台窗口 smoke、双视图 smoke 和主循环响应测试均退出 0。
测量程序完成编译；未运行完整纹理/场景性能基准，也未验证其他平台和静态构建。

### 单步生命周期与停止入口收敛

Application 流程与生命周期测试改为 run()/step()：系统失败完成收尾后进入错误终态，
不再要求同一实例恢复运行；重复观察不重放系统或诊断。线程拒绝、异常传播、析构保底保留。
异步关闭、资产加载、编辑器资产操作与布局保存测试统一 request_exit + step，覆盖在途请求、
停止阶段拒绝新操作、迟到完成与恰好一次清理。request_shutdown/advance_shutdown 已转为私有。
输入等待辅助使用 step 并显式识别停止，避免把 stopped 误当作仍在推进加载。

Windows Clang Debug 六个相关目标构建与回归通过。其他测试和测量程序的 run(n)、同步
shutdown 仍待迁移，本批没有删除这些剩余入口，也未重复完整 CPU/GPU 矩阵。

### 加载与窗口单步消费者迁移

Runtime 场景加载、Scene/Prefab 重载、属性检查和预制体准备改用 step；重载等待在停止时返回
cancelled，不把生命周期终态当成加载仍可推进。诊断测试用完整 run 验证失败收尾。
平台 smoke、原生窗口交互与绘制输入测试删除固定帧数 run 调用；窗口交互在需要确认 GPU
完成的位置显式调用 Render::finish_frames，不把 GPU 等待隐含在应用单步协议中。

Windows Clang Debug 九个相关目标构建通过，七项 CPU 回归通过；平台 smoke 退出 0，
原生窗口交互覆盖 18 次缩放、3 次最小化恢复与 5 次模拟 DPI 变化并通过。
共享加载辅助、像素验收及测量工具仍有旧入口，后续沿这些完整使用链路继续迁移。

### 有界运行入口退出

共享加载辅助、编辑器消费者、像素验收和测量程序已迁移到 run()/step()，删除 run(n) 的声明与
独立循环实现。有限帧执行不再隐式等待 GPU，截图与计数验收在需要的位置显式 finish_frames。
场景加载测量通过自己的阶段完成标记继续渲染；关闭窗口才进入 Application 停止生命周期。
完整运行工具通过 on_stop 释放 World/绑定，协作任务在 completions 阶段排空。
Tracy 帧与循环标记移动至 step，线程及空闲等待标记由 run 驱动。

本批 Windows Clang Debug 全量构建通过。213 项 CPU 回归首轮 212 项通过；Lantern 导入在
临时目录重命名时遇到 Windows 拒绝访问，单独复跑通过，尚未确定该偶发失败根因。
像素验收、平台 smoke、主循环响应、双视图均退出 0；场景测量工具使用灯廊资产的线程池与
协作 initial 模式均通过，覆盖阶段完成后继续渲染与截图。此为功能验证，不作为性能基线。
未验证完整纹理/场景压力矩阵、可选 Tracy 采集、静态构建或其他平台；PLAN-126 仍未完成。

### 公开同步关闭入口退出

Application 的 shutdown 已收为私有清理实现，普通调用者统一 request_exit + run/step。
关闭前查询、卸载场景等操作保留在调用者；停止后重复观察返回既有终态，失败不被再次清理改写为成功。
测试覆盖正常关闭、在途请求排空、失败终态、多个 Application 与窗口重开。
Windows Clang Debug 18 个相关目标构建通过，17 项 CPU 回归通过；平台窗口重开 smoke 与
资产在途请求的原生窗口关闭验收均退出 0。后者首次手动执行漏传资产目录，补齐 CTest 参数后通过。
本批未重复完整矩阵；未验证其他平台或静态构建。

### 应用时钟与渲染查询收敛

删除无人使用的 set_paused 及应用暂停状态，frame_time 只表达真实应用帧间隔与累计时间。
模拟暂停由宿主业务状态选择运行系统，不使完成分发、UI 和渲染时间归零；恢复后不补算暂停时长。
渲染统计消费者直接使用 Render::query_performance_stats，删除 Application 转发。
新增流程回归覆盖暂停两帧、UI/完成/呈现继续执行与恢复模拟，Windows Clang Debug 下通过。
Editor、Runtime、项目管理器、相关测量目标构建通过；Editor editor_demo smoke 与灯廊场景
协作加载/渲染/截图退出 0。本批未重复完整矩阵，未验证静态构建或其他平台。

### Input 查询入口收敛

Application 通过 input() 借用已有输入上下文，删除键鼠状态、事件消费和动作查询转发。
Editor UI、scene_viewer 与窗口/输入回归直接使用 Input 原生接口；平台帧推进仍由 Application 唯一负责。
借用按所属线程和有效期契约断言；不再以关闭后或跨线程调用已删除的转发接口作为错误码测试。
动作缺失、失效 generation、跨上下文混用及映射加载失败保留旧映射仍由现有回归覆盖。
Input 方法因真实跨目标消费者导出，测试链接生产实现，删除白盒测试重复编译 service.cpp。
Action Map 请求所有权尚未迁出，不以查询迁移代替整条输入加载链路完成。
本批 Windows Clang Debug 相关目标构建通过，Input 单元、输入集成、资产映射加载与 Application
流程四项 CPU 回归通过；平台 smoke、Editor、双视图、主循环响应及编辑器原生窗口交互均退出 0。
窗口交互覆盖 18 次缩放、3 次最小化恢复和 5 次模拟 DPI 切换。静态链接与其他平台留待最终矩阵。

### 宿主 Action Map 请求迁移

Runtime 与 scene_viewer 自行拥有 action_map 请求，成功后显式应用到 Input，停止回调取消请求。
Runtime 删除 input_pending 镜像标记，以请求有效性表达等待；输入就绪前不激活场景，窗口/IPC 仍推进。
测试等待辅助改用同一路径，加载失败不修改现有输入映射。旧 Application 加载实现仅待剩余测试退出，
当前仍属过渡阶段，不标记输入加载职责迁移完成。

Windows Clang Debug 四个相关目标构建通过，两项输入/流程 CPU 回归通过；Runtime 普通及协作
editor_demo smoke、scene_viewer 双视图均退出 0。未重复完整矩阵，未验证静态构建或其他平台。

### 删除 Application Action Map 旧路径

删除 begin_action_map、action_map_status、cancel_action_map 及私有请求、完成标记和自动应用逻辑。
剩余资产集成测试直接持有请求，验证加载不自动应用、显式应用、失败保留动作身份、取消、
关闭排空与复用宿主调度器重新创建 Application；删除仅服务于旧转发接口的测试。
Runtime、scene_viewer 和测试辅助显式包含 Action Map 资源契约。

Windows Clang Debug 相关目标构建通过，资产加载、输入与 Application 流程三项 CPU 回归通过；
原生窗口加载途中关闭与 Runtime 协作调度 editor_demo smoke 退出 0。
本批未重复完整矩阵，静态构建和其他平台未验证。批量资产请求与环境资源所有权仍待迁移。

### 资产管理器独立批次与场景消费者迁移

新增 asset_manager::load_batch，复用既有 resource_load_group 汇总逻辑，每个请求拥有独立批次观察，
底层资产记录、缓存及在途加载仍由同一个管理器维护。管理器 advance 推进批次并回收已放弃或
领取的汇总；停止时使请求失效。多个批次不再互斥，取消一批不取消其他观察者。
Runtime、Editor 场景加载、示例激活与场景测试辅助已迁入此入口；宿主只检查是否停止，
不等待 Application 的全局批次槽位。原 Application 槽位暂留给编辑命令和剩余测试，
退出条件为迁移全部消费者并删除 load_assets/can_load_assets 及其私有状态，不能视为此阶段完成。

Windows Clang Debug 相关目标构建通过；texture-load-application 覆盖协作与线程池模式下
多批次并存、取消隔离、独立领取，native-application 覆盖移动、取消与管理器关闭后的失效。
两项 CPU 回归通过；Runtime 协作模式、Editor editor_demo 与 scene_viewer 双视图 smoke 均退出 0。
未重复完整矩阵，静态及其他平台未验证。

### 删除 Application 批次槽位与转发

剩余编辑命令、Runtime 资源重载、测试和测量工具全部改用 assets().load_batch()。
删除 Application 的 load_assets、can_load_assets、has_asset_loading_service、批次成员与
advance_loading 转发；基础循环只推进资产管理器。场景/编辑操作独立持有请求，不再协调全局槽位。
渲染资产测试辅助也改用正式批次入口，删除私有 resource_load_group 的直接创建和手动推进。
关闭回归验证请求失效，而非调用已经删除的 Application 转发函数。
旧头文件边界测试禁止包含 Input 服务，与当前 input() 原生借用契约冲突；调整为验证借用类型，
继续保留后端与私有实现不可见检查。

Windows Clang Debug 完整构建与全部 213 项 CPU 测试通过，包含编辑器、Prefab、场景重载、
模块边界和停止生命周期。静态构建及其他平台仍留待最终验收。
像素渲染测试、Runtime 协作调度 editor_demo、Editor editor_demo 和 scene_viewer 双视图 smoke 均退出 0。

### 环境资源请求归宿主

新增 environment 只读包资产，复用资产管理器缓存、在途请求和重载；已领取快照不随重载改变，
失败保留旧内容。Runtime、Editor 自行拥有请求，在 update 中领取并向 Render 提交，背压时保留
待提交内容，停止时取消请求。Application 删除环境配置、原始文件请求和提交状态。
Render 的空包指针明确表示选择内建环境；非空空包、负强度或非有限参数拒绝，格式解析失败仍回退。
Editor smoke 等待环境请求提交后才开始采样，不再因场景先就绪而遗漏环境加载。

Windows Clang Debug 宿主与相关测试目标构建通过；资产、Application、Render 生命周期三项 CPU
回归通过，覆盖缓存重载、旧快照稳定、空包/超限失败及参数校验。原生窗口加载途中关闭退出 0。
Runtime 内建环境及坏包回退、Editor 30 帧坏包回退 smoke 均退出 0；实际渲染日志确认强度 0.6、
旋转 0.524 rad 生效。本批未验证有效环境包的视觉效果、静态链接或其他平台，也未重跑完整矩阵。

### 日志归属与诊断转发清理

删除 Application::report、diagnostic 回调和仅供该路径使用的 diagnostic_view/severity/category。
Application 运行失败直接提交 logging，具名系统名称与错误码保留；宿主通过日志服务订阅。
Render 在排空工作后输出最终统计和关闭资源诊断，直接使用 Render 的调用者也使用相同路径。
生命周期测试保留错误传播与停止行为，日志集成测试集中验证系统名称、错误码及终态不重复记录；
删除专属旧诊断回调的异常测试。

Windows Clang Debug 完整构建及七项日志、Application、资产、Render 生命周期回归通过；
Runtime 协作模式与 Editor 30 帧 smoke 退出 0，关闭日志仍含队列统计和环境参数。
本批未重复完整 CPU 矩阵，静态与其他平台尚未验证。

### 更新入口统一

生产宿主已使用具名系统，剩余测试、平台 smoke、窗口交互和测量工具迁移到 add_system(update)。
删除 application_callbacks::update 及专用执行分支、两种入口冲突检查。配置复制不再隐式复制业务更新；
重复创建 Application 的调用者显式重新注册自己的系统。更新耗时采样与 profiling scope 覆盖统一阶段。
原测试继续验证阶段顺序、异常传播、关闭拦截和输入/渲染行为，不保留专属旧回调适配。

Windows Clang Debug 引擎、资产集成、绘制提交和三项测量目标已构建；资产与绘制提交两项回归通过。
首次验证因系统提交内存耗尽中断；内存恢复后，单任务构建完成全部相关目标。
四项资产、绘制与 Application 生命周期回归通过；平台 smoke 和编辑器窗口交互退出 0，
后者覆盖 18 次缩放、3 次最小化恢复和 5 次模拟 DPI 切换。
可选 profiling 目标未启用；静态和完整矩阵留待最终验收。

### 平台启动异常的配对清理

启动审计发现平台 initialize 回调返回错误时会执行 shutdown，但抛出异常时尚未登记已进入状态，
导致部分资源清理被跳过。现于进入回调前登记配对清理，失败返回和异常传播使用同一所有权规则。
回归覆盖已取得资源后抛出异常、shutdown 恰好一次、原异常保持及无残留 Application。
Windows Clang Debug 相关目标构建及两项 Application 生命周期回归通过。
该修复不等同于最终启动装配协议完成；多 World、关闭重开和最终验证矩阵仍待验收。

### 多 World 文档隔离与重开验收

editor.scene-loader 增加同一 Application 下两个独立 World/文档的集成流程；使用正式 completions、
update、on_stop 系统驱动加载与清理，不在外部手动推进两个加载器。两者共享资产服务，同 URI
同时加载后关闭并重开主文档，预览文档的场景身份、脏状态和对象名称保持不变。退出后两文档关闭、
两个场景句柄失效，World 与任务执行器仍由宿主拥有。
Windows Clang Debug 构建及 editor.scene-loader 回归通过，覆盖线程池和协作两种调度模式。
这是文档关闭重开与多 World 隔离的证据；编辑器关闭工程后回到项目选择并打开另一工程的 UI 流程
仍需单独核对，不以本测试代替完整项目生命周期验收。

### 一次性宿主启动步骤

新增 on_start(name, function)，服务完成初始化后、初始业务状态 Enter 前按序执行一次；
返回错误跳过其余启动与业务系统，走统一停止。提前退出不执行启动，但仍执行已注册清理。
同步启动支持 void/error/result<void>；异步业务仅提交请求，继续由会话 update/状态观察。
Runtime、Editor 的环境请求已从 create 后的提前加载迁入真实 on_start，停止回调仍负责取消。
本批不改变底层平台服务在 create 中的装配，最终服务装配协议仍待收敛。

Windows Clang Debug Runtime、Editor 与流程测试构建通过。回归验证启动顺序、初始 Enter、
多帧不重放、启动失败清理与提前退出；Runtime 协作 smoke、Editor 30 帧环境回退均退出 0，
日志确认环境强度 0.6、旋转 0.524 rad 保持。静态与完整验证矩阵尚未执行。

### 编辑器关闭工程入口

新增 File → Close Project，复用文档保存确认与现有关闭收尾；只有确认执行且材质编辑未阻止关闭时，
才记录返回项目选择。最外层宿主循环等待当前会话和服务全部析构，再重新运行 Project Manager；
普通退出及运行错误不会重新进入项目选择，也不会重用命令行指定的旧工程。

Windows Clang Debug Editor 与文档动作目标构建通过；document-actions、scene-loader 两项回归通过，
覆盖关闭工程的取消、保存失败后重试与丢弃。Editor 30 帧环境回退 smoke 退出 0。
首次 smoke 错把项目文件作为 --project 参数返回 65，改为项目目录后通过；这不是代码缺陷。
实际菜单切换已补验：使用 examples/editor_demo 的两个临时副本和独立 LOCALAPPDATA，
启动工程 A，经 File → Close Project 返回项目管理器，从 Recent 打开工程 B，然后关闭 B。
同一进程的窗口标题依次为 PLAN126 project-a - Gneiss Editor、Gneiss Project Manager、
PLAN126 project-b - Gneiss Editor。A/B 均有执行帧记录，两个 Editor 会话退出日志均为结果 0，
最后进程退出。跨工具调用未取得进程 ExitCode，因此不将日志结果等同于 OS 退出码断言。
本地日志：build/plan126-reopen-check/stderr-3.log。窗口消息仅发送给测试进程；未修改用户近期工程配置。
此项验证菜单和生命周期闭环，不替代图像质量验收；隐藏 Vulkan 窗口截图不可用。

### Application 头文件依赖清理

删除 application.hpp 中已迁走职责对应的 World、场景、渲染帧、资源批次等头文件与空命名空间。
资产管理器、输入服务只保留访问器所需的前置声明；真实使用方直接包含所属模块的定义。
同步修复宿主、示例、测试夹具依赖 Application 间接包含资产、World 和任务调度器的情况。
这批不改变生命周期行为，也不将同步 create 装配认定为最终启动协议。

Windows Clang Debug 全量构建通过，213 项 CPU 回归全部通过，包含头文件边界、三个宿主相关
流程、文档与 Prefab 测试。git diff --check 通过。静态构建、跨工程完整 UI 操作仍待验收。

### Runtime 会话启动迁移

World/渲染绑定、输入映射请求、检查器初始化、Editor IPC 连接和初始场景请求迁入具名 on_start。
场景加载器在 World 建立后构造；停止时先取消加载、解除上下文借用并销毁加载器，再释放绑定与
World。未执行或部分执行的启动都由同一停止路径处理，不再以启动中途 return 绕过宿主收尾。
启动步骤错误现在统一通过 run 失败返回宿主退出码 7；工程配置解析错误仍在循环前返回。
IPC 最终通知和连接释放尚有 run 后逻辑，本批不宣称整个 Runtime 停止协议已收敛。

Windows Clang Debug Runtime 构建通过，Application 两项与 Editor Runtime launch/process/scene-loading
共五项回归通过；线程池和协作模式环境回退 smoke 均退出 0。空环境资产异步加载失败退出 7，
渲染关闭完成且任务 retained=0，验证已建立会话后的失败清理。无效 URI 和缺失文件由项目加载
提前拒绝，不作为 on_start 失败证据；步骤失败测试仍由 native-application-flow 覆盖。
Editor/示例启动迁移、基础服务延迟初始化、跨工程 UI 和静态验收仍待完成。

### Editor 会话启动迁移

资产监视、UI/布局、World/渲染绑定、文档与初始场景请求迁入具名 on_start；删除启动失败时
提前返回的重复清理，复用 on_stop。布局只有初始化成功后才保存，初始场景请求错误不再忽略。
检查关闭回调：空文档的 is_dirty 返回 false，未创建文档时不解引用文档对象。

Windows Clang Debug 全量构建通过；document-editing、scene-loader、session、asset-background-worker、
author-asset-service、startup-diagnostic 六项回归通过。Editor 线程池与协作模式各 30 帧环境回退
smoke 退出 0；空环境资产的异步加载失败退出 4，日志确认渲染服务完成关闭。
该失败发生在启动请求之后，不作为 UI 初始化失败或首次启动前 OS 关闭的交互验证。
示例启动迁移、基础服务延迟初始化、跨工程完整 UI 操作与静态验收仍待完成。

### 示例启动迁移

minimal 在 on_start 创建 World、场景和对象，停止时仅在对象有效时读取最终值，再释放 World。
scene_viewer 在 on_start 创建 World 与渲染绑定，保留 loading/viewing 状态负责场景和输入资源请求。
两者都不再在运行循环前因 World 初始化失败提前返回，部分初始化通过同一停止路径清理。
scene_viewer 的 application_create_ms 只计 Application 创建；延迟的 World 装配包含在 scene_load_ms。

Windows Clang Debug 两个示例目标构建通过；minimal-example、scene-viewer、scene-viewer-dual
三项测试通过，后两项实际运行 GPU 渲染，包含双视图独立相机与目标尺寸切换。
基础服务延迟初始化、跨工程完整 UI 操作与静态验收仍待完成。

### 基础服务启动与关闭入口收敛

create 只校验配置、保存字符串并创建应用对象；首次 step/run 装配文件、窗口、Render 和资产执行器。
首次运行前退出跳过服务启动，宿主停止回调仍执行。业务只能在启动步骤或后续运行阶段使用资产与窗口。
删除 application_callbacks：事件泵送与单调时钟由内部负责，初始化/关闭业务使用 on_start/on_stop。
唯一 on_close_request 处理器接管窗口关闭请求，宿主确认后调用 request_exit；未注册则默认退出，
显式 request_exit 不重复触发确认。Editor 与项目管理器已迁移。

生命周期测试删除仅用于注入平台回调的用例，改为覆盖启动失败、异常清理、未运行退出及终态不重放；
暂停测试验证本帧时间累积，不依赖人为时钟数值。资产、文档和窗口夹具显式驱动服务启动后再使用。

Windows Clang Debug 全量构建通过。213 项 CPU 回归中，prefab-preparation 的启动时序修复后
单独重跑通过；20 项 GPU 回归中，窗口 smoke 的纹理创建迁入 on_start 后单独重跑通过。
其余用例首轮通过。Editor、Runtime 和项目管理器 smoke 退出 0；空环境资产失败分别以
Editor 4、Runtime 7 退出并完成关闭。静态矩阵及跨工程完整 UI 操作仍未验收。

### 接口定稿与接入进度（已确认）

- 已实现：Application 类型与配置统一到 gneiss::app：application、configuration；不保留旧名称别名。
  系统适配辅助函数收进 detail，公共声明与私有实现声明分组整理。
- 调度协议及 Runtime/scene_viewer 接入已实现：启动等待采用步骤完成协议，支持 start_progress::pending/complete；不增加生命周期边的任意 bool
  判断函数表。pending 继续分发事件和完成，完成步骤不重放，失败/退出走统一停止。
  宿主负责保存请求，等待步骤不得反复提交同一任务。
- 业务状态仍用 request_state；编辑器保存确认属于业务状态，确认退出后才进入 stopping。

启动等待协议已接入 on_start：pending 保留当前步骤，完成分发继续，普通业务阶段和初始 Enter
等待全部步骤完成。生命周期测试覆盖等待成功、失败及等待中主动退出，验证已完成步骤不重放。
Runtime 已将 IPC/停止文件处理放到 completions，再将输入等待接入启动步骤；scene_viewer
也已接入一次提交与启动等待。剩余是最终审计和验证归档，不能因启动等待而停止控制消息分发。

启动等待协议的 Windows Clang Debug 引擎及两项 Application 测试目标构建通过，
native-application 与 native-application-flow 均通过，git diff --check 通过。
此增量尚未重跑完整 GPU 与静态构建矩阵。

### Application 命名统一

引擎 application/configuration、内部主循环唤醒及前置声明统一到 gneiss::app；Runtime 宿主自身
仍使用原有 gneiss::host 边界。Editor、Runtime、示例、夹具及当前参考文档同步迁移，删除旧名称。
make_system/make_start_system/make_stop_system 放入 app::detail；Application 私有声明统一置于底部。
文件仍为 application.hpp、configuration.hpp 和 schedule.hpp，不增加转发头或兼容别名。

Windows Clang Debug 全量构建通过，20 项定向回归全部通过（11 CPU、9 GPU）：
覆盖 Application 生命周期与唤醒、引擎/内部头文件边界、Runtime 场景加载/异步输入/停止协议、
Editor 与项目管理器 smoke、会话及启动诊断、minimal、scene_viewer 双视图和原生窗口。
当前源代码与 Reference/Guide 无旧 Application 类型名称残留；历史记录保留原名称。

### 示例异步启动等待接入

scene_viewer 的输入映射作为启动前置条件：request_initial_assets 一次提交输入与场景请求，
await_input 返回 pending/complete 并应用输入映射。场景及视觉资源仍在 loading 业务状态处理，
不把所有资源加载都阻塞在 starting。输入等待失败、窗口退出沿用已有 on_stop 取消请求与释放会话。
Runtime 含 IPC 命令响应及停止文件逻辑，保留其业务加载路径，后续按完整通信/业务链路继续整理。

Windows Clang Static Debug 引擎、两项 Application 测试及引擎头文件边界目标构建通过，
三项测试通过。共享构建下 scene_viewer 单/双视图回归通过；不存在的输入映射返回 not_found，
经 run 返回失败，进程退出 7。本次未执行物理窗口关闭发生于输入等待中的交互验收，
等待中退出的调度语义由 Application 流程回归覆盖；不宣称全部宿主静态矩阵完成。

### Runtime 控制分发与启动等待

将旧 update_runtime(time*, void*) 拆为具名控制分发、资产推进、场景推进与统计系统，
内部使用 runtime_context 引用。IPC 命令和停止文件进入 completions；关闭后拒绝新命令，
停止文件检查使用独立单调时钟。await_input 是最后一个启动步骤，等待期间 IPC 已连接，
场景请求已提交，但游戏初始化与普通更新尚未执行。资产更新只在业务 update 推进。
停止步骤清空资产队列并解除会话借用，删除 run 后重复资源清理。

IPC 最终结果须在应用服务停止后才能确定，因此 notify_shutdown 与宿主连接关闭仍在 run 返回后。
它们属于宿主协议尾声，不是 Application 的第二套关闭推进；不增加只为转发这一通知的生命周期 API。
之前记录的“IPC 最终通知在 run 后”保留为历史，本轮明确了其职责边界。

Windows Clang Debug Runtime 构建通过；首轮 11 项 IPC/启动/生命周期/Editor Runtime 回归通过。
集中停止清理后重跑受影响的 5 项真实进程回归全部通过，覆盖线程池/协作输入启动与失败、
停止文件、IPC 命令、场景切换及取消重试。git diff --check 通过。
