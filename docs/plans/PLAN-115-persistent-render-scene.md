<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-115：持久渲染场景与增量同步

## 状态与范围

2026-11-11：方案已确认，待开始实现。本页中的接口、结构和目录均为目标设计示意，不是当前 API。
长期边界见 [ADR-118](../decisions/ADR-118-persistent-render-scene.md)。本次先整理文档，不修改运行代码。
本计划独立跟踪，不将新能力写成 VER-052 已交付项，不延续旧版本的发布授权。
PLAN-113 的验收收尾和 PLAN-114 的测试维护仍按各自清单处理。

目标是高内聚、低耦合：World 管理游戏状态，Render 管理持久可渲染状态，通过单向批量更新同步。
采用持久实例、增量更新与每帧临时数据结合的方案，不受现有接口和改动规模约束。
不以新增 Manager 或跨层转发包装实现分层，不维护两套长期权威数据。

## 当前基线

- `render_extraction.cpp` 每帧遍历 mesh_renderer_component，复制资源强引用与世界变换；不是复制整个 World。
- `render_snapshot` 包含相机和实例列表；`render_frame_packet` 固定本帧使用的资源数据。
- Render 已持久保存 mesh/material/texture mirror；实例列表仍逐帧组装，提交 object_id 来自列表下标。
- Granit 接入使用 scene_snapshot_desc；尚未核实是否存在合适的持久场景/增量接口。
- 当前接口事实仍以 [Application](../reference/application.md) 和源码为准；本计划不表示独立渲染线程已落地。

## 目标所有权与依赖

```text
AssetManager → CPU 资产 → 渲染资源适配 ──┐
World/ECS → World 渲染同步系统 ──────────┼→ Render 前端 → 有序队列
宿主 → 每帧视图、UI、调试内容 ───────────┘                 ↓
                                           Render 持久场景与资源
                                                       ↓
                                           剔除、排序、批处理、GPU
```

| 对象 | 权威数据与责任 | 不承担 |
| --- | --- | --- |
| World | 游戏对象、层级、组件、世界矩阵计算 | GPU 对象、Render 空间索引 |
| AssetManager | 读取、解码、CPU 资产身份与依赖 | Render 实例、每个视图的可见性 |
| 资源适配 | 将 CPU 内容提交为 Render 资源，维护资产到后端身份的绑定 | 第二份资产缓存、I/O |
| world_render_binding | 实体到实例 RID、变化收集、资源绑定、同步批次 | 场景文件、Prefab、撤销历史 |
| render_service | 持久渲染场景、资源依赖、更新消费、视图及后端执行 | 回查 ECS、资产管理器、文档 |
| 宿主 | 服务装配、绑定生命周期、视口和目标、场景替换策略 | 逐实体拼装底层渲染命令 |

资源适配是职责，不强制新增公开 Service。Render 不持有上层资源对象指针；同步桥可以持有必要 CPU 资产强引用。
Render Scene 不是 SceneInstance 副本：初期一个 World 对应一个 Render Scene，多个 SceneInstance 共用它。
独立材质/模型预览可直接创建 Render Scene，不要求创建 World；一个场景可以同时接受多个 View。
灯光和网格分别存储，不提前实现通用 render_object 虚基类或另一套通用 ECS。

## 目标数据与 API

以下为签名示意；方法所属模块、所有权与行为是约束，具体命名在实施首阶段核对项目基础类型。

```cpp
struct mesh_instance_description {
  rid mesh;
  rid material;
  math::matrix4 world_matrix{1.0F};
  std::uint64_t layers{~0ULL};
  bool visible{true};
  bool casts_shadows{true};
};

class render_service {
public:
  result<rid> create_scene();
  result<scene_update> begin_update(rid scene);
  result<void> submit(scene_update&&);
  result<void> submit(frame_submission&&);
  result<void> destroy_scene(rid scene);
};

class scene_update { // 可移动，不可复制；不保活服务。
public:
  result<rid> create_mesh(const mesh_instance_description&);
  result<void> set_transform(rid instance, const math::matrix4&);
  result<void> set_mesh(rid instance, rid mesh);
  result<void> set_material(rid instance, rid material);
  result<void> set_layers(rid instance, std::uint64_t);
  result<void> set_visible(rid instance, bool);
  result<void> set_casts_shadows(rid instance, bool);
  result<void> destroy(rid instance);
};
```

begin_update 使用 result，因为关闭、无效或跨服务场景均可被正常输入触发；不将所有纯查询包装成 result。
一个更新批次绑定一个 Render Scene，实例必须属于该场景；初期不支持迁移到另一场景。
材质切换、网格切换、显隐和层掩码有明确操作，不使用任意字符串属性或通用 value 命令协议。
初期 create_mesh 表示一个网格/材质绑定；多 surface/子网格需求按真实网格模型扩展，不提前承诺完整覆盖。

Render 内部保存描述、世界包围体、空间索引位置及资源依赖。父子树只在 World；提交最终世界矩阵，
保留旋转与非均匀缩放组合产生的剪切。法线矩阵与包围体由 Render 计算，奇异矩阵行为显式校验。
网格内容或局部包围体变化必须使所有引用实例的空间数据失效；不得只观察变换变化。
未就绪资源使用明确的跳过/占位策略，资产随后就绪无需重建实例 RID。

```cpp
struct render_view {
  rid scene;
  rid target;
  math::matrix4 view;
  math::matrix4 projection;
  viewport_rectangle viewport; // 示意，实施时采用统一视口值类型。
  std::uint64_t layers{~0ULL};
};
struct frame_submission {
  std::vector<render_view> views;
  // UI/调试绘制按视图或目标明确关联；提交后拥有所有必要输入。
};
class world_render_binding {
public:
  // 示意：借用 World/Render，显式绑定场景，析构前先停止同步并解除绑定。
  world_render_binding(world&, render_service&, rid scene);
  result<void> synchronize();
};
```

相机初期是 View 输入，无需常驻相机实例；TAA 等历史能力出现时再增加稳定 View 身份。
帧请求不能借用 ECS 或调用方临时 span；临时绘制内容在提交时移交或复制所有权。
接受帧请求时固定其场景/目标依赖；较晚的销毁不能破坏较早已接受的帧，销毁后的新帧引用被拒绝。
渲染实例 RID 是稳定身份，帧内排序下标不能作为长期拾取身份；拾取回传由同步桥映射到实体。

## 更新批次与线程规则

- 第一阶段仅允许服务所属逻辑线程提交，保持单生产者有序语义；不宣称所有线程可直接调用。
- 创建先预留带 generation 的 RID；仅所属批次可引用尚未提交的预留身份，避免跨未提交批次依赖。
- 批次析构或被放弃时，回收其尚未提交身份和资源 pin；更新已有实例的未提交操作不生效。
- submit 拒绝非法批次时不发布其命令；成功消费整个批次所有权，不存在调用方需要恢复的半入队状态。
- 该提交边界不是 GPU 事务：已接受的合法操作有序执行，后端失败报告诊断，不回滚其他实例或整场景。
- 同一批次的冗余变换可合并，但不得跨创建/销毁及帧边界重排；初期保序优先，不实现复杂命令优化器。
- 非法 RID、类型不匹配、跨服务/跨场景、重复删除在可检查的前端拒绝；消费侧仍校验 generation。
- 销毁提交后前端拒绝新的引用；更早已接受的帧和更新仍按顺序执行。不用裸 RID 的有效性取代生命周期管理。
- Render 消费一个帧请求前，应用其之前的全部更新；构建该帧视图时不并发修改正在读取的状态。
- 第一阶段同线程消费；第二阶段接入独立执行线程，服务接口与场景语义保持不变。
- 同步 GPU readback 是特殊完成请求，不把普通变换/材质更新变成 Future 或每对象进度协议。

## 资源保活、销毁与关闭

RID 只标识对象；服务内部区分外部所有权、队列引用、实例依赖和在途 GPU 使用。
录制命令引用的资源必须在提交前已经被 pin，不能等消费时再尝试取得已销毁资源。
销毁外部资源拥有权意味着关闭新的外部使用，既有实例依赖继续有效；实例换绑/删除才释放依赖。
前端身份有效性与内部存储存活分开，外部失效 RID 不因此破坏既有实例的内部绑定。
GPU 最后一次使用完成后才释放后端内存；资源 payload 更新也不得改变已提交帧所观察的数据。
后端数据可使用不可变 payload、帧环形区或等价机制；不复制整个 Render Scene 来实现这一点。

destroy_scene 有序删除全部所属实例和灯光，释放其依赖，不删除其他场景使用的共享资源。
关闭顺序：停止宿主同步和新提交 → 放弃未提交批次/释放桥 → 排空或明确终止已接受工作 → 等待 GPU 安全点
→ 回收场景及资源 → 销毁服务。设备丢失不允许无限等待 fence；必须明确进入失败关闭路径。
批次不得在服务销毁后悬空回调；使用可失效的控制状态清理预留记录，不以强引用永久保活服务。
桥不得在 World 或 Render 销毁后访问借用对象；Application 或实际宿主创建并拥有绑定，确定装配析构顺序。
宿主先解除绑定并提交所属渲染实例的删除，再销毁 World 和 Render；异步后端回收仍归 Render。

## World 变化同步

world_render_binding 归属 function/scene/rendering，因为它理解 ECS、层级与实体生命周期。
World 核心不持有 render_service、不自动创建 Render Scene，也不拥有绑定；映射和同步进度不放入
world_state 或普通组件。无渲染运行不创建绑定；独立预览直接使用 Render。
暂不为该绑定建立通用 System 框架。

普通 mesh_renderer 仍持有 mesh/material 强引用，业务不接触 Render RID。
所有渲染相关组件增删改必须可观察；优先沿用受控 update_component 或 ECS 显式修改通知。
只读借用继续直接使用；可写借用必须有明确提交/修改通知，不能承诺发现任意裸指针写入。
父变换更新须标记受影响后代的世界矩阵；隐藏对象也保留正确数据，恢复可见时不使用旧变换。
删除时先记录实体到 RID 的删除事件，再释放 ECS 状态；generation 阻止复用槽位混淆。
同步桥维护映射和已消费修订，不再复制一份完整 mesh_renderer 或世界权威状态。

首次绑定枚举存量对象；之后批量收集变更，同一帧反复修改只提交最终值。
允许实施初期扫描比较作为过渡，但不能将全量扫描留下作为已完成的增量同步。
World 更新结束后、帧提交前 synchronize；编辑、撤销重做、资源重载统一经实际组件变化触发。
来源 UUID/Prefab 身份不下沉 Render；删除再创建得到新 RID，编辑器选择语义仍由文档层维护。

## 队列与可见性

更新命令可靠有序，不能随意丢弃；只允许跳过尚未执行的绘制请求，同时保留其之前全部状态更新。
队列必须有明确背压：初期限制在途帧数，队满提交返回 not_ready 且保留调用方批次，不静默丢失或无限增长。
宿主重试时不重复发送已接受批次。浏览器主线程不阻塞等待；不引入为等待而嵌套驱动主循环的接口。
队列字节上限、在途帧数采用可配置小集合，测量后给出默认值，不把性能预算写死为功能正确性。

Render 按 View/层掩码剔除；主相机不可见不代表阴影、反射或其他视图不可见。
初期遍历持久实例做视锥测试，先验证可见性与边界更新；BVH、遮挡剔除、LOD、GPU indirect 为后续计划。
渲染剔除归 Render 职责，但可以由 Granit 实现；不得在两层重复维护无必要的空间索引。
流送未来消费距离/可见性需求，文件 I/O 和 CPU 资产加载仍归资产/流送层。

## 目标目录与 Granit 边界

```text
engine/function/scene/
  world.hpp/.cpp
  rendering/world_render_binding.hpp/.cpp

engine/function/render/
  render_service.hpp/.cpp
  render_scene.hpp
  scene_update.hpp/.cpp
  render_view.hpp
  frame_submission.hpp
  detail/
    render_scene_state.hpp/.cpp
    render_commands.hpp
    render_queue.hpp/.cpp
    visibility.hpp/.cpp
    resource_storage.hpp/.cpp
    frame_resources.hpp/.cpp
  backend/...
```

这是职责布局，不强制提前创建每个文件。scene/rendering 同时依赖 World 与 Render 公共入口；
World 核心不依赖 Render，render 目录不反向包含 World/ECS。构建目标和头文件可见性也须遵循该方向。
资源适配按真实资产构建入口实现，不借目录重组创建第二套资产记录。
先核查 Granit 线程约束、资源更新、frame/fence、场景输入和剔除能力；现有 snapshot 接口可作为临时适配。
即使 Gneiss 增量同步，适配层仍可能全量生成 Granit 输入，应分别测量两层成本，不声称已消除所有全量工作。
若缺少可复用底层能力，向用户列出最小 PR 目标、接口、验收及接入方案；未经另行授权不修改 Granit。

## 实施顺序与完成条件

- [ ] P0：核查现有资源、RID、线程/帧队列和 Granit 能力；固定错误语义、销毁依赖和后端接入方案。
- [ ] P1：同线程持久 Render Scene/实例、预留 RID、更新批次、独立多视图；接通无 World 的模型预览闭环。
- [ ] P2：内部资源依赖保活、帧 payload 安全、显式场景销毁和失败关闭；消除上层销毁队列对后端时序的介入。
- [ ] P3：在 scene/rendering 实现宿主拥有的 world_render_binding 和受控组件变化通知；
  接通层级、增删、换绑、资源重载及卸载，验证无渲染 World 不需要该绑定。
- [ ] P4：按视图视锥剔除，正确更新包围体；接入 Runtime、Editor、示例及捕获/拾取入口。
- [ ] P5：有序队列、背压及独立执行线程；无独立线程平台复用同一消费器，不另设对象模型。
- [ ] P6：删除每帧全量提取协议及旧适配；迁移帧资源安全能力，不机械删除仍必要的 payload 保活。
- [ ] P7：按下列行为验收，更新 Reference/Concept 当前事实及内部头文件边界，记录平台和性能限制。

本轮不做空间树优化、流送、GPU 剔除、TAA 或通用多线程生产者框架。
CPU/GPU 创建耗时优化不作为持久场景正确性闭环的前置条件；后端能力受阻须明确记录，不能虚报 P5/P6 完成。

## 验收规则

遵循[测试指南](../guides/testing.md)：覆盖行为，不检查私有类数量或固定内部命令序列。
开发期间以编译和小型定向 CPU 测试为主；GPU/多平台统一验收安排在架构闭环后，不启动大场景或远端 CI。

- 无 World 创建模型预览；同场景多视图、不同场景独立目标，实例稳定身份与层掩码正确。
- 创建后同批更新、批次放弃、重复删除、跨场景/跨服务、旧 generation、服务关闭后的请求安全。
- 父变换、子对象删除、组件换绑、网格热重载改变包围体、隐藏再显示均正确同步。
- 一个实例/场景销毁不破坏共享资源；队列延迟及外部资源释放不产生悬空引用，在途帧不看到新 payload。
- 队满重试不重复应用；跳帧不丢状态；关闭/设备失败能收尾，浏览器不阻塞。
- 小型集成覆盖编辑、撤销、场景切换、资源异步就绪和失败降级；复用既有功能用例。
- 记录静态场景更新量、动态变换量、多视图成本、队列峰值、延迟和资源回收；不先设任意耗时门槛。

## 未决实现项与建议

没有待确认的总体架构选择。P0 需根据代码证据决定：复用何种 Granit 队列/fence、资产适配具体落点、
临时数据是否复用现有 draw list，以及后端 payload 的具体保活机制。默认复用已有机制，避免双队列和双退休系统。
若必须改变已确认所有权、增加跨仓库能力或扩大非目标，先记录差异与建议，再与用户讨论。
