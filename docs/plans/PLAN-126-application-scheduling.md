<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-126：Application 生命周期、调度与宿主状态组合

状态：收尾验收中。生命周期、具名系统与宿主启动等待已接入；实际工程 A → 项目管理器 → 工程 B 已验证。旧路径审计已完成，仍需静态宿主验证和验收归档；当前接口以 Reference 为准。
架构取舍见 [ADR-129](../decisions/ADR-129-application-scheduling.md)。本计划属于
[0.52 架构整理](VER-052-0.52.0-architecture-cleanup.md)，不包含远端发布授权。

## 当前问题与目标

实施前 `engine/include/gneiss/engine/function/application/application.hpp` 与对应
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
同一阶段按注册顺序执行；有顺序依赖的操作组合为一个 callable 或连续注册，不按类型名或隐式发现决定顺序。

每种业务状态类型有独立当前值；最初不提供嵌套状态机。状态转换只在主线程边界提交：

- completions 中的请求可在本轮转换阶段生效，update 中请求在下一轮生效。
- OnExit(old) → 更新状态 → OnEnter(new)，初始状态的 OnEnter 在服务就绪后执行一次。
- Enter/Exit 中的新请求留到下一轮，不递归转换，不修改正在遍历的系统列表。
- 同轮同目标请求合并，不同目标报告冲突，禁止最后写入者静默覆盖；应用退出优先。
- 状态进入不是事务。失败不保证自动恢复副作用，资源由明确所有者和成功初始化记录清理。

## 定稿 API 与完整使用链路

Application 创建时只保存配置和调度，首次 step/run 装配基础服务。宿主以具名 callable 注册
启动、业务和停止步骤；不提供独立 install_platform/install_assets 门面或无职责转发层。
下面的无窗口用例使用实际 API，结构与 minimal 示例一致：

```cpp
#include <gneiss/engine/function/application/application.hpp>

int run_host() {
  unsigned ticks{}; // 捕获对象先于 owner 建立，晚于 owner 销毁。
  auto created = gneiss::app::application::create({});
  if (!created)
    return 1;
  auto owner = std::move(*created);
  auto& app = *owner;
  app.on_start("session.start", [&] { ticks = 0; });
  app.add_system(gneiss::app::stage::update, "session.update", [&] {
    if (++ticks == 3)
      app.request_exit();
  });
  app.on_stop("session.release", [&] { ticks = 0; });
  return app.run() ? 0 : 2;
}
```

业务状态注册使用 on_enter(state, name, callable)、on_update、on_exit；request_state 在明确
边界提交转换。启动等待使用 on_start 返回 start_progress，停止等待使用 on_stop 返回
stop_progress；完整异步示例和失败语义以 [Reference](../reference/application.md) 为准。

system 支持 void/result<void>/error，无实际可恢复失败的操作使用 void。错误记录阶段、名称和
错误码，进入受控关闭；业务加载失败若允许重试，由宿主转换状态处理。外部循环使用同一 step
至终态，单步不隐含 GPU 等待。借用回调必须存活至停止完成；不强捕获自身 Application。
注册和状态访问的程序契约使用断言；run/step 的重入与跨线程调用保留 invalid_state，
用于外部驱动边界拒绝错误调用，不改变既有契约。意外异常传播，不逐系统吞掉。

## 异步启动、加载和关闭

启动步骤表达完成、等待完成凭据或失败；同步步骤只运行一次。等待期间继续处理必要事件与
完成通知，不能每帧重跑初始化，也不能让宿主手动 poll 每个服务。宿主停止步骤检查自身资源状态，允许未启动或部分初始化；不引入第二套启动成功状态。
文件后端未就绪时不得进入业务 OnEnter。任务执行器在装配阶段确定，不保留创建后的半初始化 attach。

业务加载由 Session 持有请求：OnEnter 提交一次，完成后经主线程消息请求下一状态。多依赖使用
已有批次请求；不新增 Application 资源记录。流程取消或退出时撤销请求并使本轮代次失效，迟到
回调不能启动旧状态。转换请求端不保活 Application，底层异步完成不能直接跨线程修改状态。

关闭顺序：拒绝新业务操作 → 停止普通系统 → 宿主取消会话任务 → 排空相关任务与完成消息 →
释放 UI、文档、绑定和 World → 释放资产、文件、渲染等服务 → stopped。具体服务顺序按真实依赖复核，资源
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

application.hpp/.cpp 保存服务装配与生命周期，schedule.hpp 保存调度值类型和模板适配；
调度容器仅在 application.cpp 使用，不另建 state.hpp 或库目标。命名空间为 gneiss::app。
宿主自己的状态、请求和组合逻辑留在 Runtime/Editor/示例；不建立第二个 Application 门面。

## 实施顺序与退出条件

- [x] A126-01：审计三个宿主、示例、测试及服务依赖；定稿 API、停止失败与借用生命周期规则。
  为成功启动、异步等待、失败、请求退出分别给出可编译最小用例；补充旧入口删除清单。
- [x] A126-02：实现最小串行调度、状态转换与共享 run/step 生命周期；先接入无 World 的项目选择界面。
  验证真实窗口/UI、初始化失败与关闭；单独调度测试通过不算本阶段闭环。
- [ ] A126-03：迁移 Runtime 与 minimal/scene_viewer 示例。Session 接管 World、加载、暂停与渲染绑定；
  完成请求驱动状态转换，移除相应 Application 专用接口。
- [x] A126-04：迁移 Editor。文档、预览、IPC 和视口使用同一调度基础；移除手动关闭推进与重复转发。
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
| 调度与状态 | 固定阶段、枚举状态、延迟转换、冲突拒绝、具名错误；类型统一到 app 命名空间 | 已审计，无旧入口 |
| 生命周期 | run/step 共用推进；基础服务延迟到启动；启动 pending/complete；停止分发完成；旧 run(n)、公开 shutdown 和平台回调已删除 | 已核对；等待静态宿主验证 |
| 所有权 | 宿主拥有 World/绑定/视图；多 World 文档隔离回归通过；真实关闭工程再开另一工程通过 | 静态宿主验证进行中 |
| 项目管理器 | selecting 状态、UI 释放与异步任务排空进入停止系统；无隐式 World | 已审计，无手动 Application 关闭推进 |
| Runtime | IPC/停止文件在 completions；输入请求由 on_start 等待；资产/场景/报告分离；统一停止清理 | 静态宿主验证进行中；未单独注入输入 pending 瞬间的 IPC 退出 |
| Editor | 会话 on_start；取消/等待/释放三段停止；实际 A → Manager → B 会话切换通过 | 已审计，无旧 Application 业务转发 |
| 示例 | minimal 自有 World；scene_viewer 一次提交输入/场景，启动等待输入；单/双视图验证通过 | 静态宿主验证进行中 |

当前接口契约以 [Application Reference](../reference/application.md) 为准。阶段复选框按实际证据更新；
A126-03/05 等待静态宿主构建与最终归档。共享全量构建与既有 CPU/GPU 回归已执行；
最近改动另有针对性回归。静态引擎、Application 测试及头文件闭包已验证，不表示全部静态宿主均验收。
停止阶段保留文档用于卸载清理，但资产编辑入口与统一内容修改入口拒绝新请求。

阶段实施与验证历史见[迁移记录](../records/2026-11-19-application-scheduling-migration.md)。
