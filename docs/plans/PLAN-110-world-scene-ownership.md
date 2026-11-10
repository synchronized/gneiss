<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-110：World 对象入口与场景归属收敛

## 状态与范围

2026-11-10：本计划完成。架构和接口迁移、全量及增量编译、分层检查和边界负例检查通过；
定向 CPU 回归 28/28 项通过，结果与修复见[验收记录](../records/2026-11-10-plan110-cpu-acceptance.md)。
此结论限于本计划的本地验收范围，不代表 GPU、跨平台或版本发布矩阵通过。

当前 API 和错误语义以 [World 参考](../reference/world.md)、
[场景实例参考](../reference/scene-instance.md) 和 [渲染参考](../reference/render.md) 为权威来源。
长期取舍见 [ADR-115](../decisions/ADR-115-world-scene-ownership.md)。
旧方案和逐批记录已移入[历史记录](../records/2026-11-10-plan-110-migration-history.md)。

非目标：替换 ECS、脚本绑定、流送、GPU 上传协议、通用事务、发布或重建编辑历史。

## 已确认边界

- World 唯一拥有 ECS 和场景槽位。GameObject、SceneInstance 是借用句柄，不引用计数，不保活对象。
- 普通对象必须属于场景，父子同场景。名称、父子、局部变换和归属集中在私有 object_state。
- 内部辅助实体可不具有对象层级；场景生成的辅助实体登记归属并随场景卸载。
- 描述 UUID、Prefab 来源和覆盖属于资产/文档。运行对象不保存这些内容，映射由装配回执交给调用方。
- 资产服务异步取得固定描述及依赖，compose 纯内存组合，instantiate 同步向显式目标场景追加对象。
- 实例化不发起 I/O、不激活相机；失败只删除本次创建对象。视觉内容就绪由上层另行推进。
- 编辑文档是编辑权威；投影维护文档地址到实体的关联。历史保存描述变化，不保存 ECS 组件裸指针。
- 普通组件操作不经过描述序列化。描述适配只承担装配、采集和描述驱动的编辑边界。
- 默认相机选择由应用决定，独立相机选择/渲染提取接口执行；World 不含相机业务方法或销毁特判。

## 实施清单

- [x] 删除独立场景集合和场景状态服务，World 直接拥有槽位。
- [x] 删除保句柄替换协议；完整重建后由调用方更新场景句柄及映射。
- [x] 删除旧准备模型，组合与实例化分离，统一 Scene/Prefab 的实例化入口。
- [x] 普通创建统一为 scene_instance::create_object；删除 World 的无归属创建入口。
- [x] 删除 scene_node_id、运行对象描述身份和旧 World node/entity 生命周期包装。
- [x] 统一受控对象数据，提供父子查询、子树迁移与场景整体卸载。
- [x] 提供 objects/roots/children 和不允许结构修改的 visit_objects。
- [x] 提供 try/add/remove_component，删除 read/set_component；约束由组件自身封装。
- [x] 分离运行描述适配和编辑器策略；属性编辑统一使用更新回调，保留描述外运行状态。
- [x] 删除重复集合安装入口 install_components；集合替换复用单组件新增/更新/删除。
- [x] 删除层级的 get_entity 和 get/set_*_for_entity 包装；实体身份不再做节点转换。
- [x] 相机选择与渲染快照提取迁出 World；变换通过 GameObject 修改。
- [x] 统一 function/scene 目录，删除旧路径兼容头。
- [x] 最终本地全量/增量编译、分层检查和边界负例检查。
- [x] 定向 CPU 运行回归：生命周期、组件行为及编辑投影链路，共 28 项通过。

## 与最初 API 示意的明确差异

这些是当前采用的实现选择，不作为遗漏接口继续扩展：

| 原示意 | 当前落地 |
| --- | --- |
| object_range | result<vector<game_object>> 句柄快照；不保活，修改结构后检查句柄 |
| World 同时 find 实体/场景 | find(entity_id) 构造借用，object(entity_id) 检查后返回结果；场景使用 scene_instance::borrow |
| 所有查询直接返回值 | 沿用 error/result 语义；name/try_component 要求有效句柄和所属线程 |
| 任意 C++ 类型直接实例化组件访问 | 组件实现单元显式实例化访问模板，不向使用方传播 EnTT |
| World::update | 调度器保留为私有能力，尚未接入运行循环，本计划不新增调度系统 |
| 实例化接收 asset_manager | 接收固定 composed_scene 和 scene_resource_bindings，避免实例化隐式加载 |
| read/set_component | 已删除；显式解引用复制或直接修改，camera_component 自身校验参数 |

world_matrix 保留完整组合矩阵；world_transform 仍作为现有 TRS 便利查询保留，
不承诺可无损表达非均匀缩放与旋转产生的剪切。

## 当前文件组织

- engine/function/scene：world、game_object、scene_instance、组件、组合、实例化、增量应用、
  camera_selection 和 render_extraction。
- engine/function/scene/detail：object_state、world_state、hierarchy、组件存储和相机选择私有状态。
- engine/resource/scene：scene/prefab/game_object/component 的描述类型。
- editor/documents：scene_document、scene_projection、scene_edit_session。

目录共置不改变边界：渲染提取可以访问 ECS，GPU 服务不反向依赖场景；
普通对象操作不引入文档、IPC 或描述序列化。具体声明位置以参考文档和源码为准。

## 最终验收

用户确认后已运行以下定向 CPU 回归；不执行 GPU、大场景或远端 CI。
具体用例和验证限制见验收记录：

1. 对象/场景生命周期：跨 World、失效句柄、重复卸载和错误线程。
2. 父子关系：循环拒绝、跨场景改父拒绝、子树迁移保留实体 ID、卸载隔离。
3. 组件：缺失查询、重复新增、删除析构、借用失效、相机校验失败保留原值。
4. 装配：重复实例化、前向引用、失败只清理新增对象、辅助实体卸载。
5. 描述更新：属性编辑和集合替换使用 update，保留组件身份和描述外运行状态。
6. Editor/IPC：增量同步、撤销重做、来源映射、完整重建后的选择重定位。

不增加新的 World 包装、来源身份或组件缓存来通过验收；
发现具体失败时按所属层修复，并更新当前参考文档。
