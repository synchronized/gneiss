<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-126：Application 生命周期、调度与宿主状态组合

状态：实施中，已接入具名业务阶段系统，已接入业务状态转换；完整生命周期迁移尚未完成。用户已确认目标方向；目标 API 为设计草案，不代表当前能力。
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

## 实施前需定稿的细节

以下属于实现细节未决，不改变已确认架构；A126-01 给出建议与完整用例后定稿：

1. 启动/停止完成凭据：优先复用现有任务和完成队列，只补最小协调协议，不实现新的通用 future。
2. 外部循环关闭失败和桌面析构保底：明确失败如何完成收尾，以及必须等待的执行范围。
3. system 标识与错误报告：优先简单稳定名称、注册顺序与 sequence；暂不增加通用依赖图。
4. 服务装配的拥有与借用执行器形式：默认唯一拥有，确有共享线程池需求时显式声明借用期限。
5. 同状态请求：建议视为无操作；需要重新加载时由明确业务操作触发，不隐式重复 Enter/Exit。

## 当前进度与剩余工作

| 范围 | 已实现 | 尚未完成 |
| --- | --- | --- |
| 调度与状态 | 五个阶段、枚举状态、延迟转换、冲突拒绝、具名错误 | 服务启动装配的最终 API |
| 生命周期 | run/step 共用推进；已删除 run(n)，分步关闭为私有实现；pending/complete 停止时分发完成 | 公开 shutdown 已移除；最终启动协议继续收敛 |
| 所有权 | 配置字符串自有；执行器在创建配置中借用；无用 service_registry 已删除；只读 window() 取代尺寸转发；宿主拥有 World/绑定/视图，默认实现已删除 | 多 World 与关闭重开验收 |
| 项目管理器 | selecting 状态、UI 释放与异步任务排空进入停止系统；无隐式 World | 最终启动协议对齐 |
| Runtime | completions/update、完整 run、停止时清除请求与卸载场景；加载器显式借用 World，检查器直接读取场景 | 输入、批次和环境请求已归宿主；诊断转发已删除；最终启动协议对齐 |
| Editor | completions/update、取消/等待/释放三段停止；文档/加载器使用宿主指定 World，删除默认 World 身份限制 | 旧业务转发清理与多 World/关闭重开验收 |
| 示例 | minimal 自有 World；scene_viewer loading/viewing、停止清理、双视图尺寸切换 | 输入加载和批次已迁移；最终启动协议对齐 |

当前接口契约以 [Application Reference](../reference/application.md) 为准。计划各阶段保持未完成，
不能把多个宿主使用新 run 等同于整体所有权迁移完成。旧 attach_task_executor 已从源码删除，
测试配置显式指定执行器，等待辅助不再隐式装配或吞掉 attach 错误。
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
