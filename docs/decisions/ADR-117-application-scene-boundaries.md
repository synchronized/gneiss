<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-117：应用装配、资产、运行世界与编辑文档边界

- 状态：已接受，尚未实施。
- 日期：2026-11-11。
- 实施计划：[PLAN-113](../plans/PLAN-113-application-scene-boundaries.md)。

## 背景

当前 runtime 同时包含应用主循环、活动及退休 World、场景打开和重载、视觉资源批次。
仅将 scene_service 改名为 scene_controller 不能解决生命周期和业务策略混合的问题。
引擎场景目录同时包含运行实例能力、Prefab 回放及编辑候选生成，需明确使用边界。

## 决策

1. Application 是应用级服务的装配和调度根，直接拥有 World，不经场景服务间接拥有。
   World 唯一拥有运行实体和场景槽位；game_object、scene_instance 仍为非拥有句柄。
2. AssetManager 负责所有资产的统一异步加载、依赖处理和缓存。结构依赖完成与视觉资源
   就绪分别表达；不得为场景另建资产缓存或文件读取通道。
3. compose、instantiate、apply_scene_update 是有明确输入输出的场景算法，不因步骤不同
   各设一个常驻 Service。组合和实例化不执行 I/O，更新算法不保存文档、历史或来源状态。
4. 场景追加、替换和激活时机属于宿主策略。引擎不默认要求活动、候选、退休三份 World，
   不承诺整场景失败回滚。初期不新增 scene_controller 替代现有大服务。
5. EditorDocument 持有编辑内容，历史记录和投影由编辑器管理；投影借用 World。
   Prefab 数据位于 resource，运行展开与覆盖回放属于引擎，编辑覆盖生成属于编辑器。
6. 渲染提取属于 render 模块的前端桥接，允许读取 World 并生成独立渲染输入；
   RenderService 的后端实现仍不得依赖 World、ECS 或编辑器。
7. application 目录承载共享应用装配，host/runtime 与 host/editor 承载具体入口和宿主策略。
   通用资产批次只组合既有请求，不携带编辑器 session/revision，不引入第二套资产所有权。

## 与既有决策的关系

保留 [ADR-115](ADR-115-world-scene-ownership.md) 的 World 所有权、对象句柄和场景归属规则。
局部取代 [ADR-116](ADR-116-loading-request-api.md) 中候选 World、显式激活和退休 World
作为共享场景请求协议的设计；其查询不推进、所属线程消费、取消不破坏共享加载等规则继续适用。
上述是目标边界，现有 API 在 PLAN-113 完成前仍按当前 Reference 执行。

## 影响

Application 的 World 在普通场景切换时保持稳定；被卸载实例的对象句柄仍会失效。
打开场景的调用方需要显式处理资产完成、实例化和卸载顺序；复杂宿主策略可封装为私有操作，
但不能再次包办资产缓存、World 所有权和编辑历史。编辑器预览可独立拥有额外 World。
迁移需要同步调整宿主、编辑器、测试、构建边界与文档，不能只移动文件。

## 替代方案

- 原样保留并重命名 scene_service：迁移小，但保留间接所有权和多职责协调。
- 全部合入 Application：文件减少，但主循环承担所有业务状态。
- 为每个步骤新建 Service：可独立命名，但增加生命周期、调度入口和跨对象依赖。

选择少量所有者和无长期状态算法，按真实使用需求再提炼共享协调能力。
