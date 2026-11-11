<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Application 生命周期与主循环

## 原生入口与所有权

仓内宿主使用 `src/application/application.hpp` 的 `gneiss::runtime::application`，
配置为 `application_configuration`。`application::create(config)` 返回
`engine::result<std::shared_ptr<application>>`；成功值拥有已初始化对象，失败返回错误并清理部分初始化状态。
调用方检查结果后移出拥有引用；不创建 Application RID 或注册表项。
完整 Application C API 与公共创建描述已删除。动态游戏模块也已撤销。

Application 拥有 World、平台状态、资源服务及可选窗口。`native_world()` 返回借用对象，
调用方不得销毁 Application 拥有的 World。关闭后借用 World 句柄失效。
最后一个 Application 引用必须在创建线程释放，析构执行关闭；也可显式调用 `shutdown`。
初始化失败会回收部分状态，已经进入初始化的平台回调与 shutdown 配对。

原生配置中的字符串在创建期间借用，回调由 Application 保存；回调捕获的借用对象必须
存活至关闭结束。不要让回调强捕获自身 Application，避免引用环。

## 加载请求

Application 每帧在 update 回调前统一处理文件完成、资产管理器和共享资源批次。
自定义宿主循环调用 `run(1)` 驱动完整一帧；业务回调内不重入 run。
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

## 平台与窗口

`application_configuration::enable_window` 默认为 false，允许空回调无窗口模式。
窗口默认 1280×720。Granit 是固定构建依赖；`enable_window=false` 时，Application
不创建窗口、不初始化 GPU 设备，仍可执行逻辑循环与 CPU 资产加载。窗口模式不允许同时提供平台 initialize、
poll_events 或 shutdown 回调，业务 update 与时间回调仍可使用。

Application 私有持有 `engine::window_service`，负责窗口生命周期及键盘/鼠标事件轮询。
`engine/platform/window_service.hpp` 不包含 Granit 头；具体窗口类型、原生句柄提取和事件转换
封装在 `.cpp` 的 PImpl 中。服务只能由所属线程使用，不可复制或移动；原生窗口信息仅供内部渲染桥接，
不能超过服务生命周期。Application 先关闭渲染，再销毁窗口服务。
当前只有 Granit 实现，没有后端工厂或运行期实现选择；暂未接入 Granit headless 离屏渲染。

`get_window_size` 返回当前客户区尺寸。Win32 路径采用物理像素，与指针坐标一致；
无窗口模式返回配置尺寸。最小化时尺寸可能为零，恢复后再提交渲染。

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
渲染资源数量通过 render_service::live_resource_count() 查询。

### 来源重载基线

激活回执的 reload_state 由调用方按需保留，包含 World 借用、场景 ID、不可变来源及对象地址关联。
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
