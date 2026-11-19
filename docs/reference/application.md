<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Application 生命周期与主循环

## 原生入口与所有权

仓内宿主使用 `engine/include/gneiss/engine/function/application/application.hpp` 的 `gneiss::runtime::application`，
配置为 `application_configuration`。`application::create(config)` 返回
`gneiss::result<std::shared_ptr<application>>`；成功值拥有已初始化对象，失败返回错误并清理部分初始化状态。
调用方检查结果后移出拥有引用；不创建 Application RID 或注册表项。
完整 Application C API 与公共创建描述已删除。动态游戏模块也已撤销。

Application 拥有 World、平台状态、资源服务及可选窗口。`native_world()` 返回借用对象，
调用方不得销毁 Application 拥有的 World。关闭后借用 World 句柄失效。
最后一个 Application 引用必须在创建线程释放，析构执行关闭；也可显式调用 `shutdown`。
初始化失败会回收部分状态，已经进入初始化的平台回调与 shutdown 配对。

原生配置中的字符串在创建期间复制，回调由 Application 保存；回调捕获的借用对象必须
存活至关闭结束。不要让回调强捕获自身 Application，避免引用环。

创建配置的 `executor` 借用宿主任务执行器，必须覆盖 Application 关闭。提供执行器时在创建过程中
完成资产服务装配；省略时不启用资产任务。协作执行器由宿主的 completions 系统执行 run_ready。
不提供创建后 attach；是否装配资产执行器在创建时确定。

## 加载请求

Application 每帧在 update 回调前统一处理文件完成、资产管理器和共享资源批次。
自定义宿主循环调用 `step()` 驱动一轮并处理停止状态；业务回调内不重入循环。
场景打开、来源重载、实例化及替换策略由 Runtime/Editor 宿主加载器维护。
示例直接使用资产请求与组合/实例化接口；examples/support/scene_activation.hpp 仅复用示例装配。

- 普通资产和场景：`app.assets().load<T>(uri)` 返回资产请求，Scene/Prefab 也使用同一入口。
- 多资源编辑：`app.load_assets(sources, reload)` 返回 `asset_batch_request`；缓存命中可立即就绪。
  单个批次占用一个观察槽位，`take()` 领取完成值并释放槽位，未完成返回 not_ready。
- `can_load_assets()` 表示应用接受请求、已配置加载执行器且共享批次空闲；仅所属线程查询。
  它不判断场景切换策略，也不表示所有单资产请求均已完成。

查询和领取回执不驱动加载。单资产观察者可以复制，取消只影响自身观察者；批次请求只能移动。
请求不保活 Application，共享资产工作由 asset_manager 管理。宿主负责释放自己的观察者和批次，
不得在 Application 关闭后继续访问借用的服务。视觉批次失败由宿主记录诊断，不回滚已实例化对象。

实例化向指定场景追加对象；替换时由宿主卸载旧场景并清理借用句柄，World 保持不变。
来源重载入口位于 runtime_scene_loader，编辑器采用文档投影；Application 不提供场景专用请求。
Host 的 session/revision 由 IPC 协调器保存，不是资产加载 API 的参数。

## 主循环

`run(max_frame_count)` 每帧轮询事件、计算时间、调用 update 并提交渲染。
update 只接收帧时间，诊断回调只接收诊断事件，关闭请求回调不带参数；宿主状态由回调捕获。
`max_frame_count` 为零表示持续运行至退出，非零限制本次执行帧数。
多次调用 run 延续帧序号与累计时间；重入、跨线程或关闭后运行返回 `invalid_state`。

`request_exit()` 请求在回调返回后退出。`close_requested` 返回 false 可拦截平台关闭，
返回 true 或未提供回调时正常退出。暂停仍轮询事件和调用 update，但 delta_ns 为零。
回调返回失败时保留原生错误结果；意外异常向执行边界传播，自有宿主输出致命诊断后终止。
正常退出仍执行资源清理；致命终止不承诺完整析构。

## 具名系统与业务状态

首次 run 前通过 add_system 注册 completions、input、update、synchronize、present 系统。同阶段按注册顺序执行，
名称在该 Application 内唯一；首轮运行冻结注册。completions 在正常帧和停止排空期间执行，
用于协作任务与宿主完成分发，不受业务状态和退出标志过滤；input 在状态转换后、update 前执行。
update 后刷新 World 变化，再执行 synchronize、
present，最后通过现有 Render 路径提交。旧 callbacks.update 与具名 update 系统不能混用。

业务状态使用宿主自己的枚举，不要求反射注册。每种枚举类型有独立当前值：

```cpp
enum class mode { loading, ready };
auto& app = *application;
app.init_state(mode::loading);
app.on_enter(mode::loading, "begin_loading", [&] { session.begin_loading(); });
app.on_update(mode::loading, "loading", [&](const gneiss::frame_time& time) {
  session.draw_loading(time);
});
app.on_exit(mode::loading, "leave_loading", [&] { session.clear_loading_ui(); });
app.on_update(mode::ready, "game", [&](const gneiss::frame_time& time) {
  session.update(time);
});
// 主线程完成回调：检查请求结果；不同目标冲突时保留首次请求。
auto changed = app.request_state(mode::ready);
```

这些系统接受无参数或 frame_time 参数，返回 void、result<void> 或过渡期的 error。
Application 保存 callable，借用捕获须存活至关闭结束；不得强捕获自身 Application。

初始 Enter 在文件服务就绪后执行一次。完成分发后、业务更新前统一提交转换：
Exit(old) → 修改当前值 → Enter(new)。update 或 Enter/Exit 中发起的新请求留到下一轮，
包括发给其他枚举状态类型的请求。多种状态按初始注册顺序处理，不构成跨状态事务。
state<T>() 查询当前枚举值；初始 Enter 前返回配置值。

同目标待处理请求合并，不同目标返回 invalid_state 并保留首次请求；当前状态的无待处理同值
请求不重复 Enter/Exit。请求退出后拒绝新转换（cancelled），停止后续具名系统。
未注册的状态类型、跨线程访问、运行后注册属于程序契约错误。Enter/Exit 返回失败时报告
系统名称；再次 run 返回同一失败，不重放已有副作用。此时宿主应执行关闭，不尝试继续业务。

无参数 run() 驱动 step() 至 stopped；可恢复失败先完成关闭，再返回最早的运行错误。
step() 返回 result<app::lifecycle>，starting/running/stopping 为进行中，stopped 或错误为终态。
请求退出后不会再执行普通系统；停止阶段继续处理完成消息。单步不执行每帧 GPU finish_frames，
服务最终关闭仍遵循后端回收协议。意外异常向外传播，桌面析构作为同步清理保底。

迁移期间旧 run(max_frame_count) 仍保留有界执行与 GPU 帧收尾，不能在使用 step 后调用旧入口。
项目选择界面、Runtime、Editor 和两个示例均使用 run()；旧入口仍供未迁移的测试使用。
业务 OnExit 只用于状态转换。

## 宿主停止清理

首次运行前用 on_stop(name, callable) 注册具名、无参数的停止步骤。调用方可取消并排空自己的
任务、释放 UI 或会话资源；首次 request_shutdown 或直接 shutdown 在停止资产、文件、渲染
服务之前按注册顺序执行一次。析构保底使用相同入口，未运行就退出也会执行已注册清理。
初始化只完成一部分时，清理函数必须根据自身拥有的资源状态处理，不能假定 Enter 已成功。

进入清理前 accepts_requests() 已为 false，不再允许运行循环、注册系统或发起业务状态转换。
已有服务仍可借用以释放资源，不能递归启动关闭。资源释放入口不应以“不再接受新请求”为由
跳过析构。回调捕获必须覆盖 Application 的关闭与析构过程。

清理返回 void、result<void> 或 error；失败记录系统名称，继续后续清理及服务关闭，最终返回
首次宿主清理错误（优先于后续服务关闭错误）。request_shutdown 的成功只表示关闭请求已接收，
清理失败通过 advance_shutdown 的终态结果返回。直接 shutdown 返回最终结果。
异步步骤返回 app::stop_progress 或 result<app::stop_progress>；pending 保留当前步骤，
下一轮继续检查既有任务完成，complete 或错误则移动至下一步骤。等待期间仍分发文件和资产完成，
不会提前关闭服务。步骤自身必须避免重复提交工作；任务作用域可直接使用 try_close_scope。
error::not_ready 仍是错误，不代替 pending。同步 shutdown/析构可等待，外部循环应使用 step 排空。
意外异常保持传播，析构时按既有 noexcept/致命异常策略处理，不承诺异常后所有清理完成。

项目选择界面、Editor 和 Runtime 已用 on_stop 非阻塞排空任务、会话并释放 UI，由 run() 统一完成服务关闭。

## 平台与窗口

`application_configuration::enable_window` 默认为 false，允许空回调无窗口模式。
窗口默认 1280×720。Granit 是固定构建依赖；`enable_window=false` 时，Application
不创建窗口、不初始化 GPU 设备，仍可执行逻辑循环与 CPU 资产加载。窗口模式不允许同时提供平台 initialize、
poll_events 或 shutdown 回调，业务 update 与时间回调仍可使用。

Application 私有持有 `gneiss::window::service`，负责窗口生命周期及键盘/鼠标事件轮询。
`engine/include/gneiss/engine/platform/window/service.hpp` 不包含 Granit 头；具体窗口类型、原生句柄提取和事件转换
封装在 `.cpp` 的 PImpl 中。服务只能由所属线程使用，不可复制或移动；原生窗口信息仅供内部渲染桥接，
不能超过服务生命周期。Application 先关闭渲染，再销毁窗口服务。
当前只有 Granit 实现，没有后端工厂或运行期实现选择；暂未接入 Granit headless 离屏渲染。

`window()` 返回借用的只读 Window 服务；无窗口或关闭后返回空指针。所属线程通过
`window()->surface()` 读取实际客户区尺寸；无窗口不伪造配置尺寸。Win32 路径采用物理像素，
与指针坐标一致。最小化时尺寸可能为零，恢复后再提交渲染。平台事件仍仅由 Application 泵送。

日志回调在消费线程执行，其关闭与借用规则见[日志参考](logging.md)。

## 加载推进责任

文件完成分发、资产管理器推进及回收由 Application 主循环统一调用私有实现。
状态查询和领取回执不推进工作；resource_load_group 只提交单资产请求并汇总结果。
场景打开与来源重载由宿主观察资产完成通知；加载进度与 IPC 会话状态归宿主。
关闭时 Application 请求资产管理器停止并推进排空；Scene 和批次只放弃自己的请求与实例。

### World 生命周期

Application 直接拥有 World，native_world() 的地址在普通场景切换期间保持稳定，关闭后失效。
Runtime 宿主显式卸载其上次打开的实例；编辑器关闭旧文档时卸载对应实例，其他场景不受影响。准备阶段不创建运行对象或预留视觉
资源身份。场景卸载后旧对象/场景句柄失效，不能将 World 地址稳定理解为所有句柄永久有效。
Runtime 打开场景直接使用 assets().reload<scene>()，就绪后组合并调用 instantiate；
其视觉资源批次由 Runtime 持有。结构加载失败保留旧实例，
取消只放弃本观察者；视觉资源失败记录日志，不撤回已经创建的场景。
Application 的 can_load_assets() 只表示共享资源批次可提交；宿主查询自己的视觉批次。
编辑器同样直接请求资产并实例化，将固定内容建立为文档快照，实例化回执交给编辑会话绑定。
加载失败或取消保留原文档、选择及撤销历史；成功后关闭旧文档并采用新实例。
编辑器通过 editor_scene_loader::visual_loading() 观察自己的视觉批次，不创建 Runtime 重载基线。
编辑器打开状态由 scene_open_state 表示 idle/loading/opened/failed/cancelled，active() 从状态推导。
加载界面显示不定进度；不展示无法跨帧观察的实例化阶段、无效计数或旧清理等待状态。
旧场景服务及其打开/激活/重载请求协议已删除。无待退休 World，也不提供专用场景退休统计；
渲染资源数量通过 render::service::live_resource_count() 查询。

### 来源重载基线

激活回执的 reload_state 由调用方按需保留，包含借用的 scene_instance、不可变来源及对象地址关联；World 从场景句柄取得。
Runtime 保存当前场景的基线；编辑器采用文档投影，不长期保留此基线。
重载显式传入该引用，Application 不保存最后激活场景的隐式来源。
Runtime 生产路径已改用 runtime_scene_loader::reload：直接请求 Scene/Prefab 资产，收到完成通知
后调用 rebase_scene_update/apply_scene。take_reload 只领取回执，不执行加载或更新。
宿主的 scene_open_result、scene_reload_state、scene_reload_result 定义在
host/runtime/runtime_scene_loader.hpp，只保存绑定检查器和来源更新所需的信息。
不包含旧加载进度、退休统计或未采样的准备/激活耗时。
新视觉依赖通过宿主持有的批次加载。
同一 World 的多个场景可以各自保留基线；当前协调器仍串行处理重载请求。
空引用、跨 World 或已失去同步的基线被拒绝；已卸载的场景不能重载。
基线不保活 World，调用方不能在其销毁后访问借用指针；重载期间不得自行修改基线。

来源更新的内存算法位于 engine/function/scene/scene_update：rebase_scene_update 读取运行状态，
调整调用方的临时组合结果；apply_scene 应用差异。前者不修改 World，不加载资产；失败后临时
组合结果应丢弃。加载协调负责资产引用、重载基线及完成回执。

## 普通帧同步

有窗口时 Application 拥有一个 world_render_binding，普通帧先同步 World 变化，
再查询相机并提交 render::frame_request（UI、调试输入随帧提供）。无窗口运行不创建绑定。
最小化跳过绘制，但仍同步对象删除和资源换绑；关闭先释放绑定，再销毁 World。
普通帧背压由 Render 处理，Application 不再领取可复用 packet 或枚举全部网格。
截图 capture_frame 使用相同的绑定和 frame_request，暂保持同步诊断回读；
普通帧与截图均不再从 World 提取全量对象列表。完成和迁移边界见 [Render](render.md)。
