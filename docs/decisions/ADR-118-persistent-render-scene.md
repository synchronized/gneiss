<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-118：持久渲染场景、增量同步与帧视图边界

- 状态：已接受，待实施。
- 日期：2026-11-11。
- 实施与目标 API：[PLAN-115](../plans/PLAN-115-persistent-render-scene.md)。

## 复核后的决策更新

2026-11-11：用户确认[整体设计复核](../records/2026-11-11-render-design-review.md)中的直接业务 API，
以及 `gneiss::render`、单一 PImpl 和 Granit 私有后端边界。下文已替换旧公开批次决策。
该更新是目标设计确认，不代表现有运行代码已迁移；实施进度与具体接口以 PLAN-115 为准。

## 背景

现有 World 提取每帧可渲染实例列表，Render 已缓存 GPU 资源，但实例没有持久身份。
这种隔离简单可靠，却重复遍历、复制、组装实例，不利于复用空间结构和支持多个视图。
用户确认以高内聚低耦合为目标，可以调整现有架构，不以兼容迁移代码为设计限制。

## 决策

采用持久渲染实例、单向批量增量更新与每帧视图/临时内容结合的模型。

1. World 唯一拥有游戏状态；Render 唯一拥有持久渲染状态。渲染场景不复制父子树、Prefab 或文档。
2. Render Scene 独立于场景文件和 SceneInstance，可用于无 World 预览；多个 View 可观察同一场景。
3. 渲染同步桥归属场景逻辑侧，是唯一同时认识 World 与 Render 的集成点；通过可观察组件变化维护实体到实例 RID。
   由 Application 或实际宿主创建并拥有；World 核心不持有 Render 或绑定，Render 不反向依赖 World/ECS。
   无渲染 World 不创建绑定，独立渲染预览不需要 World；具体目录与生命周期见实施计划。
4. 服务前端提供直接创建/修改/删除实例的类型化操作，可靠批量传输在内部实现；
   不公开 scene_update 录制/提交协议，不以通用 value 或反射路径更新渲染实例。
5. Render 不回查 ECS/资产管理器，不持有上层资源对象；资源适配负责提交 CPU 内容，资产层仍负责 I/O。
6. Render 内部管理外部所有权、队列保活、实例依赖及在途 GPU 使用，RID 本身不承担保活。
   实例关联资源身份，按有序发布取得内容版本；不永久固定首次 payload。
7. 单个业务操作完整接受，创建成功后的 RID 可直接继续使用；多次操作允许部分成功，
   不提供整批或整场景 GPU 事务回滚。可预期输入错误前端返回，异步后端错误报告诊断。
8. 更新可靠有序，帧请求可以按策略跳过但不能丢状态；容量和背压必须明确，不无限排队、
   不阻塞浏览器主线程。不预设每个 setter 都让业务重试，优先由服务及集成绑定处理调度。
9. 持久场景先同线程实现，再接独立执行线程；线程模式不得产生第二套 API 或场景模型。
10. 帧数据安全仍必须保留，可采用不可变 payload/帧存储等机制，不再复制整份 World 实例快照。

11. 入口命名为 gneiss::render::service，只拥有一个 impl；前端校验元数据与消费侧完整状态分开，
    资源和场景存储平级。World 渲染绑定仍在逻辑场景侧，CPU 资产不迁入 Render 命名空间。
12. gneiss::render::backend::granit 为私有实现：负责 Granit 设备、GPU 资源、快照适配、绘制与退休。
    Service 负责引擎身份、依赖、操作顺序和关闭协调；Backend 不再复制服务 API，也不重新定义业务生命周期。
    不引入公开 Backend 服务、i_backend、后端工厂或插件体系；现阶段只实现真实需要的具体 Granit 后端。
13. 剔除是 Render 职责，但可委托 Granit；不重复维护空间索引。服务头不传播执行器或 Granit 类型。

本决策细化既有资产与 Render 边界，不改变 World 唯一所有者和宿主负责场景切换的 ADR-117。
现有资源及帧捕获机制能复用则复用；实现细节和迁移阶段以 PLAN-115 为唯一来源。

## 备选方案与代价

| 方案 | 收益 | 主要代价 |
| --- | --- | --- |
| 每帧全量快照 | 数据隔离和生命周期简单 | 静态数据重复提交，多视图与空间索引复用受限 |
| Render 直接访问游戏对象 | 少一份表示，初期接入容易 | 线程/析构耦合，Render 无法独立运行 |
| 所有内容都作为持久对象 | 统一身份 | UI、调试线等临时内容也承担生命周期成本 |
| 已选混合方案 | 持久状态、视图与临时数据职责明确 | 必须可靠追踪变更、处理队列和资源依赖 |

公开批次的优势是取消和整批校验，但会将预留身份、取消与提交恢复暴露给调用方；当前没有必须
整批原子的使用场景，因此采用直接 API。其代价是绑定必须记录已接受进度，不能盲目重放多次操作。
单一 PImpl 配合具体后端隔离实际职责；暂不为假设的第二后端建立多态框架。

不承诺新方案在所有工作负载下更快；静态/动态比例、多视图和适配层开销需要测量。
不能因为接口经过队列就声称线程安全，也不能将接收成功等同于 GPU 完成。

## 外部实践参考

以下是设计依据，不表示复制其完整实现或要求 Gneiss 具有同等功能。

- [Godot RenderingServer 实现](https://github.com/godotengine/godot/blob/master/servers/rendering/rendering_server_default.h)：RID 与队列封装。
- [Godot 场景剔除](https://github.com/godotengine/godot/blob/master/servers/rendering/renderer_scene_cull.cpp)：持久实例与按视图处理。
- [UE 线程渲染](https://dev.epicgames.com/documentation/unreal-engine/threaded-rendering-in-unreal-engine)：游戏组件与渲染代理的数据隔离。
- [O3DE Atom 术语](https://docs.o3de.org/docs/atom-guide/glossary/)：渲染组件经 Feature Processor 提交数据。
- [Flax SceneRendering](https://github.com/FlaxEngine/FlaxEngine/blob/master/Source/Engine/Level/Scene/SceneRendering.cpp)：注册 Actor、缓存空间数据并收集绘制；其直接 Actor 访问不是本项目的目标边界。
- [Unity BRG](https://docs.unity3d.com/6000.0/Documentation/Manual/batch-renderer-group-how.html)：实例数据与剔除输出分离，仅代表公开 BRG 路径，不推断全部内置实现。

Granit 为当前后端，是否需要增量场景接口须另行核查；未经授权不修改其仓库。
