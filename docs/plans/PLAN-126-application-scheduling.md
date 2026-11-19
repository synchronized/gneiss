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
| 生命周期 | run/step 共用推进，pending/complete 停止，停止时完成分发 | 删除测试中的 run(n) 与分步关闭旧入口 |
| 所有权 | 配置字符串自有；执行器在创建配置中借用；无用 service_registry 已删除；只读 window() 取代尺寸转发 | 默认 World、视口、绑定移交 Session |
| 项目管理器 | selecting 状态、UI 释放与异步任务排空进入停止系统 | Application 本身不再创建默认 World |
| Runtime | completions/update、完整 run、停止时清除请求与卸载场景；加载器显式借用 World，检查器直接读取场景 | 输入加载、暂停与旧业务转发清理 |
| Editor | completions/update、取消/等待/释放三段停止；文档/加载器使用宿主指定 World，删除默认 World 身份限制 | 旧业务转发与 Application 默认 World 清理 |
| 示例 | minimal 自有 World；scene_viewer loading/viewing、停止清理、双视图尺寸切换 | scene_viewer 的 World/视口归属迁移 |

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

过渡边界：Application 暂时仍为未迁移示例和测试创建默认 World；注册 present 系统的宿主
不再触发旧默认呈现，避免一轮双重提交。这不是最终协议，退出条件是 scene_viewer 和相关
测试迁移完成，随后一并删除默认 World、默认呈现及业务转发接口。

本批验证：Runtime、Editor、原生窗口交互目标构建通过；Runtime 普通及协作 smoke、
Editor editor_demo smoke、原生窗口交互测试均退出 0。未以此代替双视图示例迁移验收。
