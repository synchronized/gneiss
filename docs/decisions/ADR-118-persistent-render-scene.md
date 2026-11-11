<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-118：持久渲染场景、增量同步与帧视图边界

- 状态：已接受，待实施。
- 日期：2026-11-11。
- 实施与目标 API：[PLAN-115](../plans/PLAN-115-persistent-render-scene.md)。

## 背景

现有 World 提取每帧可渲染实例列表，Render 已缓存 GPU 资源，但实例没有持久身份。
这种隔离简单可靠，却重复遍历、复制、组装实例，不利于复用空间结构和支持多个视图。
用户确认以高内聚低耦合为目标，可以调整现有架构，不以兼容迁移代码为设计限制。

## 决策

采用持久渲染实例、单向批量增量更新与每帧视图/临时内容结合的模型。

1. World 唯一拥有游戏状态；Render 唯一拥有持久渲染状态。渲染场景不复制父子树、Prefab 或文档。
2. Render Scene 独立于场景文件和 SceneInstance，可用于无 World 预览；多个 View 可观察同一场景。
3. 渲染同步桥是唯一同时认识 World 与 Render 的集成点；通过可观察组件变化维护实体到实例 RID。
4. 服务前端接收类型明确的更新批次，队列和后端协议为私有实现；不以通用 value 或反射路径更新渲染实例。
5. Render 不回查 ECS/资产管理器，不持有上层资源对象；资源适配负责提交 CPU 内容，资产层仍负责 I/O。
6. Render 内部管理外部所有权、队列保活、实例依赖及在途 GPU 使用，RID 本身不承担保活。
7. 批次保证有序接收，不提供整场景 GPU 事务回滚；可预期输入错误前端返回，异步后端错误报告诊断。
8. 更新可靠有序，帧请求可以按策略跳过但不能丢状态；背压显式，不无限排队、不阻塞浏览器主线程。
9. 持久场景先同线程实现，再接独立执行线程；线程模式不得产生第二套 API 或场景模型。
10. 帧数据安全仍必须保留，可采用不可变 payload/帧存储等机制，不再复制整份 World 实例快照。

本决策细化既有资产与 Render 边界，不改变 World 唯一所有者和宿主负责场景切换的 ADR-117。
现有资源及帧捕获机制能复用则复用；实现细节和迁移阶段以 PLAN-115 为唯一来源。

## 备选方案与代价

| 方案 | 收益 | 主要代价 |
| --- | --- | --- |
| 每帧全量快照 | 数据隔离和生命周期简单 | 静态数据重复提交，多视图与空间索引复用受限 |
| Render 直接访问游戏对象 | 少一份表示，初期接入容易 | 线程/析构耦合，Render 无法独立运行 |
| 所有内容都作为持久对象 | 统一身份 | UI、调试线等临时内容也承担生命周期成本 |
| 已选混合方案 | 持久状态、视图与临时数据职责明确 | 必须可靠追踪变更、处理队列和资源依赖 |

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
