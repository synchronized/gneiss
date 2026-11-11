<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Render 整体设计复核

## 后续状态

本记录保存复核当时的分析，下文“待确认”是当时状态。用户随后确认直接业务 API、render 命名空间、
单一 PImpl 与 Granit 私有后端边界；已统一写入 [PLAN-115](../plans/PLAN-115-persistent-render-scene.md)
及 [ADR-118](../decisions/ADR-118-persistent-render-scene.md)。后续以两者为准，不将历史草案当作当前实现。

## 结论与状态

2026-11-11，复核基线为 `f8febed0`。本次只修改约束与设计文档，不修改运行接口。
保留 [ADR-118](../decisions/ADR-118-persistent-render-scene.md) 已确认的持久渲染场景、RID、
单向同步与宿主拥有绑定的边界。建议改变的是对外更新协议：采用直接业务操作，内部可靠批量传输，
不要求调用者录制/提交 `scene_update`。该协议变化尚待确认，不作为已实现接口。
实施进度唯一来源仍是 [PLAN-115](../plans/PLAN-115-persistent-render-scene.md)。

## 源码证据与问题

| 当前事实 | 影响 | 建议 |
| --- | --- | --- |
| `render_service.hpp` 包含执行器头，公开帧存储复用、完成领取、CPU prepare/share/status 等接口 | 宿主和资产调用者接触内部流程 | 保留服务入口，按调用角色收窄公开面；执行器/帧存储不成为普通渲染用法 |
| `application.cpp::render_frame` 依次复用 packet、提取 World、捕获资源、提交 | Application 了解逐帧表示与资源捕获细节 | 宿主只同步绑定、提供视图并调用 render |
| 新 `scene_update.cpp` 录制时预留 RID、固定 payload，提交时重新校验整批 | 调用者承担取消、预留身份和批次失败协议 | 普通操作即被服务接受；内部合并传输，不要求调用方批次事务 |
| `render_scene_state` 持久实例与旧 `render_snapshot` 绘制路径尚未连接 | 两条路径不能互相证明正确 | 下一实施单元必须包含实际绘制消费者和旧路径退出条件 |
| 新实例固定已就绪 payload；旧资产路径可以 reserve 后 replace | pending 与热重载语义不一致 | 实例持有资源身份依赖；帧执行时固定 payload 版本 |
| `asset_construction` 已负责包装、资源依赖和退休，持有 Render 引用 | 再建公开资源服务会重复职责 | 优先收敛该适配职责，不再增加资源 Manager |
| `game_object::try_component()` 返回可写借用，注释明确不自动发送通知 | 无法可靠发现任意原地写入 | 渲染相关组件采用受控编辑/明确通知；不把扫描比较算成最终增量同步 |
| `render_extraction.cpp` 使用世界 TRS，而 GameObject 已有 world_matrix | 剪切可能无法准确表达 | 同步最终世界矩阵，Render 计算法线矩阵与包围体 |
| `threaded_render_executor` 已有帧/命令队列；resumable 可重排，帧可替换 | 不能直接宣称现有队列满足可靠更新时序 | 复用执行器，场景命令可靠保序；补命令容量和背压，不另建线程体系 |

源码入口：
[服务](../../src/engine/function/render/render_service.hpp)、
[宿主装配](../../src/application/application.cpp)、
[阶段批次](../../src/engine/function/render/scene_update.cpp)、
[资产适配](../../src/engine/function/asset/detail/asset_construction.hpp)、
[组件接口](../../src/engine/function/scene/game_object.hpp)、
[执行器](../../src/engine/function/render/render_executor.cpp)。

文件问题也应随职责处理：`scene_update` 同时出现在逻辑 Scene 与 Render 目录，虽然不是重复类，
但含义容易混淆；`render_extraction` 位于 Render 却直接依赖 World 内部，是迁移后应消失的反向依赖。
不通过给这些文件再加一层转发来解决。

## 推荐对象边界

| 对象/职责 | 拥有的状态 | 不承担 |
| --- | --- | --- |
| AssetManager 与 CPU 资产 | URI、读取解码、依赖、资产缓存 | GPU 执行、场景剔除 |
| 资产到 Render 的内部适配 | 资产到渲染身份的关联、内容发布、外部拥有权释放 | 第二份 URI 缓存、独立加载器 |
| world_render_binding（逻辑场景侧） | entity→instance RID、已确认修订、待同步变化、必要资产引用 | Prefab、文档历史、第二份 World 状态 |
| render_service（唯一渲染服务） | 前端身份/生命周期元数据、可靠命令入口、消费侧场景/资源、执行器 | 读取 World、文件 I/O、编辑器策略 |
| render_view / frame_request（值） | 相机、场景/目标 RID、层掩码、自有临时绘制输入 | 常驻对象列表、可借用的 ECS 内存 |
| 后端私有实现 | Granit 对象、GPU 在途版本与退休 | 资产路径、游戏对象身份 |

Render 内部区分前端身份表、消费侧场景状态与 GPU 存储，但不各自增加公开 Service。
前端只保留校验所需身份、归属与生命周期信息；实例完整状态由消费侧维护。
资源身份关联到当前不可变内容版本，历史帧保留其使用的版本；两者不是两份独立资产缓存。
初期仍由 Granit 快照适配驱动绘制，不声称消除后端全量工作。

## 推荐 API 草案

以下为设计示意，`frame_request`、回读请求及资源发布入口均未实现。没有引入新的命名空间迁移。

```cpp
class render_service {
public:
  result<rid> create_scene();
  result<void> destroy_scene(rid scene);

  result<rid> create_mesh_instance(rid scene, const mesh_instance_description&);
  result<void> set_transform(rid instance, const math::matrix4&);
  result<void> set_resources(rid instance, rid mesh, rid material);
  result<void> set_visibility(rid instance, const instance_visibility&);
  result<void> destroy_instance(rid instance);

  result<void> render(frame_request&& frame);
  // 回读用专门异步请求返回图像结果；普通绘制不暴露帧完成轮询。
};
```

`create_mesh_instance` 与资源的 `create_mesh` 明确区分。网格/材质换绑作为一个局部操作接受，
避免观察到中间不匹配组合；显隐、层掩码、阴影开关组成有业务意义的小值类型。
不会为了减少函数数量使用字符串属性或任意 value 命令。

资源适配使用服务的资源创建/发布/释放能力。prepare/校验归资产解码或纯函数，帧资源捕获、
执行器及 buffer 回收为私有实现；不是再建一个只转发这些函数的公开 facade。
资源初始化失败与内容尚未就绪分开，pending 资源身份可以绑定，GPU 就绪不是资产 load 成功条件。

## 五条完整调用链

以下是责任与控制流示意，省略常规 result 展开；不是可直接编译的现有示例。

### 1. 加载资源并显示

1. 宿主创建 File/Asset/Render 服务，安装资产到 Render 的适配；无渲染模式不安装该适配。
2. `assets.load<mesh>(uri)` 表示统一资产加载入口的示意；File 读取、解码与依赖管理仍在资产侧。
3. 适配预留渲染身份，发布不可变内容；资源 ref 的生命周期决定何时释放外部拥有权。
4. 普通业务把 CPU 资产 ref 放进组件；绑定负责转换为 RID。业务不手动 prepare/upload/poll GPU。
5. pending 时跳过实例或使用明确占位策略；完成后发布到同一资源身份，实例 RID 不变。
6. 资源失败返回资产诊断；可渲染对象之外的场景继续存在。最后一个外部 ref 释放后，
   实例依赖和在途帧仍可保活资源；新外部绑定使用已释放 RID 必须失败。

资源身份依赖与内容版本分开是前置条件，不能继续用“实例固定首次 payload”代替。

### 2. 无 World 的模型预览

```cpp
auto scene = render.create_scene();
auto instance = render.create_mesh_instance(*scene, description); // Render RID + 世界矩阵
render.set_transform(*instance, rotation);
render.render(make_preview_frame(*scene, target, camera));
render.destroy_scene(*scene); // 包含实例；共享资源外部拥有权单独释放
```

预览控制器借用 Asset/Render，从同一内部资产适配获取资源身份；不自行创建第二套缓存。
失败检查在每个操作的接受点。成功返回的实例 RID 可立即用于后续修改，不存在调用方可见的未提交身份。
不需要创建 World、SceneInstance、虚构游戏对象或显式更新批次。

### 3. World 同步

```cpp
// 宿主装配阶段创建场景和绑定，并保证它们先于 World/Render 关闭。
auto render_scene = render.create_scene();
world_render_binding binding{world, render, *render_scene};

world.update(dt);
binding.synchronize();
render.render(make_game_frame(*render_scene, target, camera));
```

首次绑定枚举已有对象，之后使用变更修订/事件。普通组件增删、受控编辑以及层级变换传播均产生变化。
建议给需要通知的组件提供 `edit_component<T>(fn)` 或类型自身受控 setter，调用结束统一发布修订；
受观察类型不再承诺通过长期可写裸指针修改仍能被发现。具体接口迁移需覆盖现有所有写入者。
变换变化必须包含后代，组件资源内容重载走资源发布，无需 World 再提交同一个实例。

绑定仅在 Render 接受操作后更新已同步修订/映射；not_ready 时保留待办，不重复创建已接受实例。
多对象同步不承诺整批回滚。宿主可以跳过该次绘制等待同步补齐，但不能撤销已经接受的 Render 更新。
binding 的进度归其内部，Application 不再遍历实例或保存待提交 packet。

### 4. 多视图与临时内容

一次 `render(frame_request)` 接收多个 view；每个 view 明确 scene、target、viewport、矩阵和层掩码。
同一 Render Scene 无需按相机复制实例，剔除按视图执行。UI/调试数据随请求拥有并关联目标/视图。
目标创建/resize/销毁由 Render 提供独立资源入口，窗口句柄只在目标接入边界出现。

整份帧请求接受前校验场景/目标并固定内部依赖；队满返回 not_ready，失败不消费调用方请求。
后续销毁不破坏已接受帧。普通绘制的异步失败通过服务诊断报告；回读请求必须有唯一成功/失败终态，
不由宿主通过 finish_frames + 全局完成队列猜测对应结果。

### 5. 关闭与失败

1. 宿主停止新任务/World 修改与帧提交，取消资产新请求。
2. 绑定显式 detach；停止变化订阅并销毁所属渲染实例/场景，not_ready 时由关闭流程继续交付。
3. 资产适配停止发布，释放外部渲染拥有权；关闭 Render 输入后不得再调用资源发布。
4. Render 排空已接受命令，对回读请求逐一完成或失败，等待安全点并退休 GPU 对象。
5. 最后销毁 World、Render 与窗口；借用绑定必须已解除。

桌面与浏览器复用相同状态机，浏览器不阻塞等待。析构可清理未完成状态，但不把不确定时长的
设备排空隐藏成普通小句柄析构。设备丢失进入失败关闭，不能无限等待 fence。
这是目标生命周期，当前 shutdown API 的异步化也应在迁移范围内明确实施。

## 接受、执行、版本与背压

- 每个修改操作接受时校验、取得内部依赖并可靠入队；创建成功后 RID 已可用。
  入队失败不暴露 RID、不修改前端身份；内部临时预留由服务自行撤销。
- 只保证一个业务操作的接受边界，不引入任意整场景事务。消费侧按顺序执行，GPU 失败通过诊断报告。
- 实例依赖资源身份；资源内容更新也是同一有序流中的命令。消费某帧时使用该位置的版本，
  GPU 在途引用保留旧版本直到安全退休，不能事后读取前端最新 payload。
- 合并/打包是内部传输优化，只能在不跨帧和生命周期边界时进行；不能跨删除/创建合并。
- 更新命令有容量上限，满时 not_ready；可替换帧只丢绘制，绝不带走之前状态更新。
  后台消费独立于是否提交绘制，最小化或无视图也能完成更新与释放。
- 应用主循环已有服务更新/完成分发入口应复用；业务不新增 advance/poll/flush 调用链。
  单线程平台在宿主统一调度处消费，不能把 render(frame) 作为唯一释放触发点。

## 方案取舍与确认点

| 方案 | 优点 | 代价 |
| --- | --- | --- |
| 公开 scene_update 批次 | 可取消；可为整批提供接收校验；便于批量生产 | 预留 RID、跨批引用、取消、失败恢复成为调用方协议；当前未见必须整批原子的使用场景 |
| 直接业务 API，内部批量（推荐） | 普通使用直接；没有未提交 RID；独立预览与同步共用入口 | 多操作可部分接受；绑定需记录接受进度；合并和背压由服务实现 |
| 返回有行为的 render_scene / render_instance 句柄 | 对象式调用集中，易于自动补全 | 增加借用/拥有语义和一层 API；当前用户已选择 RID 边界，暂不引入 |

需要确认的主要变更是第一项：是否采用推荐的直接 API，取消公开批次事务。
World 修改通知是增量同步的必要工作，不是可省略的优化；可先用受控编辑入口，避免扩展通用事件框架。
同意总体方案后，在 PLAN-115 替换目标 API/批次条款，并同步更新 ADR-118 第 4/7 项。
本次只记录建议，不把它写成已接受决策，也不删除已有代码。

## 下一实施单元及退出条件

确认后按完整调用链推进，不再先堆一组无消费者的类型：

1. 无 World 预览：资源身份依赖 + 可靠创建/更新/删除 + 实际绘制 + 关闭。
   覆盖 pending→ready、热重载、外部释放后实例存活；消除阶段实现固定首次 payload 的限制。
2. World 接入：受观察组件写入口、层级变更、绑定和实际 Runtime/Editor 画面一起迁移。
   此单元退出时删除 extract_render_snapshot 的对象列表链路，资产/UI 不再向 Render 提交上层对象 ref。
3. 多视图、回读与队列背压：统一帧输入、结果归属和关闭状态机；移除宿主 packet 复用/捕获/完成轮询。
   删除公开 scene_update 与旧提交协议必须随其消费者迁移，不额外保留兼容别名。

测试保护可观察的身份、版本、释放、画面和失败语义，不固定私有命令数量或类结构。
若 Granit 快照适配仍导致全量成本，应单独测量和记录，不因此再增加一套公开场景模型。
