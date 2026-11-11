<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-115：持久渲染场景与增量同步

## 状态与范围

2026-11-11：已开始实施。P0 接入核查与 P1 的 CPU 场景/更新批次基础已落地；绘制、World 同步及其余阶段尚未完成。
除下节明确列出的实现外，本页接口、结构和目录仍为目标设计示意。
长期边界见 [ADR-118](../decisions/ADR-118-persistent-render-scene.md)。当前接口与阶段限制见 [Render](../reference/render.md)。
本计划独立跟踪，不将新能力写成 VER-052 已交付项，不延续旧版本的发布授权。
PLAN-113 的验收收尾和 PLAN-114 的测试维护仍按各自清单处理。

目标是高内聚、低耦合：World 管理游戏状态，Render 管理持久可渲染状态，通过单向批量更新同步。
采用持久实例、增量更新与每帧临时数据结合的方案，不受现有接口和改动规模约束。
不以新增 Manager 或跨层转发包装实现分层，不维护两套长期权威数据。

## 已确认的复核结论

2026-11-11：用户确认直接业务 API、`gneiss::render` 命名空间、单一 PImpl 外壳，
以及 Service 与 Granit 私有后端分离。本文已替换旧公开批次方案，作为后续实施目标。
[复核记录](../records/2026-11-11-render-design-review.md)保留当时的分析与方案取舍。
本次只更新文档；现有代码仍使用 `render_service` 和公开 `scene_update`，不能描述为迁移完成。

- 对外直接创建、修改和删除实例，内部可靠批量传输；不要求调用者管理预留 RID 或批次事务。
- `render::service` 只持有 `unique_ptr<impl>`；资源、场景与执行职责在内部组合。
- `render::backend::granit` 为私有实现命名空间，不是第二个公开 Service，也不新增后端插件框架。
- 资源身份与内容版本分离；实例依赖身份，帧使用按命令顺序确定的内容版本。
- World 侧绑定保持原有归属，补齐受观察组件的修改通知；按完整调用链迁移和删除旧路径。

## 本轮实现与接入核查

- Granit 锁定版本的 `pipeline/scene.hpp`/`scene.h` 只有快照创建与销毁；创建会校验并复制输入，
  没有持久实例修改入口。初期由 Gneiss 保存实例，后端临时生成 Granit 快照；暂不要求跨仓库修改。
- 既有 `threaded_render_executor` 已负责线程、帧/命令序号和可替换帧。后续可靠场景更新复用这一执行器，
  使用一次执行命令；不能使用允许重新排队的 resumable 策略，也不能随可替换帧丢弃。
- 既有 `render_resource_snapshot` 通过不可变共享 payload 保证旧帧数据不变；保留该能力。
  新增实例当前固定已就绪 payload，资源身份 pin、未就绪填充及热重载刷新仍属于 P2，不能标记完成。
- `render_service` 已提供 create_scene/begin_update/submit(scene_update&&)/destroy_scene。
  当前只管理 CPU 状态，不需要窗口/GPU，也尚未连接实际绘制。更新提交同线程生效。
- `scene_update` 支持预留实例、变换/资源/层掩码/显隐/投影阴影标记与删除；预校验后发布整个批次。
  放弃批次回收预留 RID，跨服务/场景和过期 generation 被拒绝；关闭后批次可安全销毁。
- 定向 CPU 验证覆盖批次取消、旧批次失败不部分应用、独立字段更新、共享 payload 保活与关闭。
  Windows Clang Debug 的 Engine、Runtime、Editor 与渲染测试目标构建通过；render_scene、render_frame_packet、render_executor 三项测试通过。
  尚未验证 GPU、多视图、World 同步和其他平台。

后续按已确认边界完成资源身份 pin 和持久场景绘制入口，再接 World 变化通知与绑定；
不能把当前 CPU 批次基础视为 P1 模型预览闭环或 P3 增量同步完成。

## 迁移基线

- `render_extraction.cpp` 每帧遍历 mesh_renderer_component，复制资源强引用与世界变换；不是复制整个 World。
- `render_snapshot` 包含相机和实例列表；`render_frame_packet` 固定本帧使用的资源数据。
- Render 已持久保存 mesh/material/texture mirror；实例列表仍逐帧组装，提交 object_id 来自列表下标。
- Granit 接入使用 scene_snapshot_desc；接口核查结果见上文。
- 当前接口事实仍以 [Application](../reference/application.md) 和源码为准；本计划不表示持久场景已接入既有独立渲染线程。

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
| world_render_binding | 实体到实例 RID、变化收集、资源绑定、已接受进度 | 场景文件、Prefab、撤销历史 |
| render::service | 持久渲染场景、资源依赖、更新消费、视图及后端执行 | 回查 ECS、资产管理器、文档 |
| 宿主 | 服务装配、绑定生命周期、视口和目标、场景替换策略 | 逐实体拼装底层渲染命令 |

资源适配是职责，不强制新增公开 Service。Render 不持有上层资源对象指针；同步桥可以持有必要 CPU 资产强引用。
Render Scene 不是 SceneInstance 副本：初期一个 World 对应一个 Render Scene，多个 SceneInstance 共用它。
独立材质/模型预览可直接创建 Render Scene，不要求创建 World；一个场景可以同时接受多个 View。
灯光和网格分别存储，不提前实现通用 render_object 虚基类或另一套通用 ECS。

## 目标命名空间与内部所有权

- 渲染入口及值类型归 `gneiss::render`：`service`、`mesh_instance_description`、`view`、`frame_request`。
- 队列、身份表、场景/资源存储归 `gneiss::render::detail`。
- Granit 实现归 `gneiss::render::backend::granit`；引用第三方命名空间使用 `::granit` 避免名称遮蔽。
- CPU 资产 `gneiss::mesh/texture/material`、公共 `rid` 与 `math` 类型不迁入 Render。
- world_render_binding 保持逻辑场景侧归属，不因使用渲染接口搬进 Render 命名空间。
- 文件名随职责迁移收敛为 service 等；不保留旧名称兼容别名，不进行无关模块改名。

```text
render::service
  └─ impl
      ├─ 前端身份、归属和生命周期元数据
      ├─ 执行器与可靠有序命令传输
      └─ 消费侧状态
          ├─ 资源身份、不可变内容版本与依赖
          ├─ 场景、实例、包围体与可见性数据
          └─ backend::granit::backend
```

这是职责划分，不要求每个框成为类。Service 外壳只有一个 `unique_ptr<impl>`，不再并列拥有
resource_storage 和 implementation。impl 负责装配、接受操作和关闭协调，不继续堆放全部 GPU 实现。
资源存储与场景存储平级，资源存储不拥有场景。前端不复制消费侧完整实例状态，只维护接受校验所需信息。
消费侧状态由执行线程访问；异步完成通过拥有型结果交回，不因同属 impl 就允许跨线程任意读写。

## Service 与 Backend 边界

| 职责 | Service 及其内部状态 | Granit 私有后端 |
| --- | --- | --- |
| 对象身份 | RID 类型、generation、服务/场景归属与逻辑销毁 | 不再管理引擎对象归属 |
| 资源生命期 | 外部拥有权、实例依赖、内容版本与逻辑存活 | 上传、GPU 对象、在途引用与安全退休 |
| 操作顺序 | 接受、可靠有序传输、背压和完成分发 | 按消费顺序执行，不自行重排业务命令 |
| 场景和帧 | 持久实例、视图、目标、临时输入与剔除策略 | Granit 快照适配、绘制、呈现和回读 |
| 能力与诊断 | 引擎自己的能力值和诊断接口 | 查询 Granit 能力并转换后端错误 |

Service 决定逻辑资源何时不再被需要，Backend 保证 GPU 使用结束后才释放实际对象，不能各管一套退休协议。
Backend 不再复制一套 create_scene/set_transform 等服务 API，也不回查 ECS、AssetManager 或文档。
它接受消费侧提供的渲染输入及资源版本，管理 GPU 表示；初期转换成 Granit scene_snapshot。
异步 GPU 使用必须持有必要版本，不长期借用会被消费侧修改的数组或对象。

现有 implementation 中 renderer、surface/swapchain、pipeline、environment、GPU mirrors、上传与回读实现
移到后端；执行器、请求接受、帧回收和完成分发留在 Service 内部。后端普通接口不泄漏 Granit 类型，
其私有实现可以使用 Granit。后端类型不出现在 service.hpp，执行器头也不再被该头传播。
先用一个具体 Granit 后端，不引入 i_backend、虚函数接口、工厂或动态插件；第二个真实实现出现后再提炼。

## 目标数据与 API

以下为尚未实现的签名示意；行为与边界是约束，资源/目标/关闭接口的完整签名在首个闭环中确定。

```cpp
namespace gneiss::render {
struct instance_visibility {
  std::uint64_t layers{~0ULL};
  bool visible{true};
  bool casts_shadows{true};
};
struct mesh_instance_description {
  rid mesh;
  rid material;
  math::matrix4 world_matrix{1.0F};
  instance_visibility visibility{};
};
struct view {
  rid scene;
  rid target;
  math::matrix4 view_matrix;
  math::matrix4 projection_matrix;
  viewport_rectangle viewport; // 示意，复用统一视口值类型。
  std::uint64_t layers{~0ULL};
};
struct frame_request {
  std::vector<view> views;
  // 自有 UI/调试输入，明确关联视图或目标。
};
class service final {
public:
  result<rid> create_scene();
  result<void> destroy_scene(rid scene);
  result<rid> create_mesh_instance(rid scene, const mesh_instance_description&);
  result<void> set_transform(rid instance, const math::matrix4&);
  result<void> set_resources(rid instance, rid mesh, rid material);
  result<void> set_visibility(rid instance, const instance_visibility&);
  result<void> destroy_instance(rid instance);
  result<void> render(frame_request&&);
private:
  struct impl;
  std::unique_ptr<impl> impl_;
};
}
```

资源 mesh 与 mesh_instance 明确区分。网格/材质换绑是一个业务操作，不暴露中间只换一半的状态。
不使用字符串属性或任意 value 命令协议。初期实例为单网格/材质绑定，多 surface 按真实需求扩展。
父子树只在 World，提交完整世界矩阵，保留剪切；法线矩阵、包围体和奇异矩阵校验归 Render。
网格版本/局部包围体变化也必须更新实例空间数据。pending 资源允许绑定，采用明确跳过或占位策略，
随后就绪和热重载不重建实例 RID。相机初期只作为视图输入，暂不增加常驻相机或稳定 View 身份。

资源准备/格式校验归资产解码或纯函数；资源适配使用服务的身份创建、内容发布和拥有权释放能力。
优先收敛既有 asset_construction，不新增公开资源 Manager。普通宿主不再调用 prepare_frame_packet_storage、
capture_render_frame_packet 或完成领取来协调绘制；截图使用专门异步请求并获得唯一终态。

## 操作接受与线程规则

- 服务所属逻辑线程调用，单生产者有序接受；不承诺任意线程直接调用。
- 创建成功后 RID 已被接受，可以立即用于后续操作；内部预留在接受失败时自行回收，不向调用方暴露。
- 单个业务操作接受前校验并取得依赖，失败不产生半接受状态。多次调用允许部分成功，不提供整批回滚。
- 内部可靠打包/合并不改变外部语义，不跨帧、创建/销毁边界重排；不保留公开 scene_update 事务协议。
- 非法 RID、类型/归属错误、重复删除在前端拒绝，消费侧仍防止 generation 混淆。
- 接受成功不等于 GPU 完成；异步后端失败报告诊断，不回滚其他实例或整个场景。
- 销毁被接受后拒绝新的外部引用，较早接受的帧和命令仍按序安全执行。
- 消费一帧前应用其之前的更新；该帧确定内容版本，不能读取后来发布的前端最新 payload。
- 初期可同线程验证消费者，随后接既有执行器；不新建平行线程体系或第二套 API。

## 五条调用链与宿主职责

1. **资源加载**：统一资产入口读取/解码/处理依赖 → 内部适配建立渲染身份并发布内容 →
   组件持有资产强引用。业务不手动等待 GPU；失败保留资产诊断，不要求整个场景失败。
2. **独立预览**：适配取得资源 RID → create_scene → create_mesh_instance → set_transform →
   render(frame_request) → destroy_scene → 释放资源外部拥有权。不创建 World 或虚构游戏对象。
3. **World 同步**：宿主拥有 World/Render/绑定；world.update 后 binding.synchronize，再 render。
   绑定保存实体到实例映射与已接受修订，失败不重复已接受操作；Application 不再拼实例列表。
4. **多视图/回读**：一帧携带多个视图与明确目标，场景实例不按相机复制；帧接受后拥有全部必要输入。
   回读是专门异步请求，不能依赖全局完成队列和 finish_frames 推测结果。
5. **关闭**：停止生产 → 绑定解除订阅和实例 → 资产适配停止发布并释放拥有权 →
   Render 处理已接受命令与回读终态 → GPU 安全退休 → 销毁服务和窗口。具体约束见下节。

详细调用示意见[复核记录](../records/2026-11-11-render-design-review.md)，以本页已确认目标为准。

## 资源保活、销毁与关闭

RID 只标识对象；服务内部区分外部所有权、队列引用、实例依赖和在途 GPU 使用。
操作接受前取得内部资源身份依赖，不能等消费时再通过已销毁的外部 RID 查询资源。
实例持有稳定资源身份；内容发布形成有序版本，帧执行按序取得并保留版本，不固定首次 payload。
销毁外部资源拥有权意味着关闭新的外部使用，既有实例依赖继续有效；实例换绑/删除才释放依赖。
前端身份有效性与内部存储存活分开，外部失效 RID 不因此破坏既有实例的内部绑定。
GPU 最后一次使用完成后才释放后端内存；资源 payload 更新也不得改变已提交帧所观察的数据。
后端数据可使用不可变 payload、帧环形区或等价机制；不复制整个 Render Scene 来实现这一点。

destroy_scene 有序删除全部所属实例和灯光，释放其依赖，不删除其他场景使用的共享资源。
关闭顺序：停止宿主同步和新提交 → 解除绑定并停止资产适配发布 → 排空或明确终止已接受工作 → 等待 GPU 安全点
→ 回收场景及资源 → 销毁服务。设备丢失不允许无限等待 fence；必须明确进入失败关闭路径。
回读和异步完成不得在服务销毁后悬空回调；每个已接受请求都必须成功完成或明确失败。
浏览器关闭按异步状态机推进，不阻塞主线程，也不通过嵌套主循环等待。
桥不得在 World 或 Render 销毁后访问借用对象；Application 或实际宿主创建并拥有绑定，确定装配析构顺序。
宿主先解除绑定并提交所属渲染实例的删除，再销毁 World 和 Render；异步后端回收仍归 Render。

## World 变化同步

world_render_binding 归属 function/scene/rendering，因为它理解 ECS、层级与实体生命周期。
World 核心不持有 render::service、不自动创建 Render Scene，也不拥有绑定；映射和同步进度不放入
world_state 或普通组件。无渲染运行不创建绑定；独立预览直接使用 Render。
暂不为该绑定建立通用 System 框架。

普通 mesh_renderer 仍持有 mesh/material 强引用，业务不接触 Render RID。
所有渲染相关组件增删改必须可观察；当前 try_component 可写借用不自动通知。
目标使用受控 edit_component<T>(fn) 或组件 setter/显式修改通知，迁移全部相关写入者；接口名称待实施核对。
只读借用继续直接使用；可写借用必须有明确提交/修改通知，不能承诺发现任意裸指针写入。
父变换更新须标记受影响后代的世界矩阵；隐藏对象也保留正确数据，恢复可见时不使用旧变换。
删除时先记录实体到 RID 的删除事件，再释放 ECS 状态；generation 阻止复用槽位混淆。
同步桥维护映射和已消费修订，不再复制一份完整 mesh_renderer 或世界权威状态。

首次绑定枚举存量对象；之后批量收集变更，同一帧反复修改只提交最终值。
允许实施初期扫描比较作为过渡，但不能将全量扫描留下作为已完成的增量同步。
World 更新结束后、帧提交前 synchronize；编辑与撤销重做统一经实际组件变化触发。
资源内容重载经有序资源发布更新渲染身份，无需 World 为同一个实例重复换绑。
来源 UUID/Prefab 身份不下沉 Render；删除再创建得到新 RID，编辑器选择语义仍由文档层维护。

## 队列与可见性

更新命令可靠有序，不能随意丢弃；只允许跳过尚未执行的绘制请求，同时保留其之前全部状态更新。
队列必须有明确容量与背压，不静默丢命令、不无限增长、不阻塞浏览器主线程。
不预设每个 setter 都向业务暴露队满重试；优先由服务内部打包/调度及绑定保存待同步变化。
如果操作返回可重试的 not_ready，必须表示尚未接受，且不产生副作用；绑定只记录成功接受的进度。
帧队满时失败不消费调用方 frame_request；已接受命令不随跳帧丢失。
具体命令容量与背压机制在闭环实施前明确并验证，不将未实现策略写成现有保证。
消费与完成分发复用宿主统一调度；没有视图、窗口最小化时仍能处理更新和释放，不能只靠 render(frame) 驱动。
队列字节上限、在途帧数采用可配置小集合，测量后给默认值，不把性能预算写死为功能正确性。

Render 按 View/层掩码剔除；主相机不可见不代表阴影、反射或其他视图不可见。
初期遍历持久实例做视锥测试，先验证可见性与边界更新；BVH、遮挡剔除、LOD、GPU indirect 为后续计划。
渲染剔除归 Render 职责，但可以由 Granit 实现；不得在两层重复维护无必要的空间索引。
流送未来消费距离/可见性需求，文件 I/O 和 CPU 资产加载仍归资产/流送层。

## 目标目录与 Granit 边界

```text
engine/function/scene/
  rendering/world_render_binding.hpp/.cpp

engine/function/render/
  service.hpp/.cpp
  render_types.hpp
  detail/
    service_impl.hpp
    resource_storage.hpp/.cpp
    scene_storage.hpp/.cpp
    executor.hpp/.cpp
  backend/granit/
    backend.hpp/.cpp
    resource_storage.hpp/.cpp
    scene_adapter.hpp/.cpp
    pbr_shader_resolver.hpp/.cpp
```

这是职责布局，不强制提前创建每个文件。scene/rendering 同时依赖 World 与 Render 公共入口；
World 核心不依赖 Render，render 目录不反向包含 World/ECS。构建目标和头文件可见性也须遵循该方向。
资源适配按真实资产构建入口实现，不借目录重组创建第二套资产记录。
先核查 Granit 线程约束、资源更新、frame/fence、场景输入和剔除能力；现有 snapshot 接口可作为临时适配。
即使 Gneiss 增量同步，适配层仍可能全量生成 Granit 输入，应分别测量两层成本，不声称已消除所有全量工作。
若缺少可复用底层能力，向用户列出最小 PR 目标、接口、验收及接入方案；未经另行授权不修改 Granit。

## 实施顺序与完成条件

以下清单已按复核结论重排；旧 CPU 批次基础是迁移输入，不等于新阶段完成。

- [ ] P0：收敛命名空间、单一 PImpl 和 Service/Backend 边界，明确资源身份/版本、背压和关闭语义。
  随首个实际调用链调整；不以只移动文件或增加空壳作为该阶段完成。
- [ ] P1：资源身份依赖、直接实例 API、真实 Granit 绘制与关闭一起接通无 World 模型预览。
  覆盖 pending→ready、热重载、外部释放后的实例存活；删除被替代的公开 scene_update 与首次 payload 固定逻辑。
- [ ] P2：接通有序资源版本、帧安全和 GPU 退休，复用执行器并验证设备失败关闭；
  上层退休只交还拥有权，不介入后端时序。P1 必要安全能力必须同时实现，不能以阶段编号推迟。
- [ ] P3：受控组件修改、层级变化、world_render_binding 与 Runtime/Editor 实际绘制一起迁移；
  验证无渲染 World 无需绑定，删除 extract_render_snapshot 对象列表链路。
- [ ] P4：多视图、目标、视锥剔除、临时输入和异步回读/拾取闭环；
  删除宿主 packet 复用/资源捕获/普通帧完成轮询，保留内部必要 payload 保活。
- [ ] P5：完成命令容量、背压、跳帧和单/多线程消费验收；不得因无视图停止释放，不新增平行队列服务。
- [ ] P6：清理旧命名/协议/适配及无消费者抽象，迁移有效测试；取消的批次事务契约不再保留测试或兼容别名。
- [ ] P7：按下列行为验收并更新 Reference/Concept 当前事实及内部头文件边界，记录平台和性能限制。

本轮不做空间树优化、流送、GPU 剔除、TAA 或通用多线程生产者框架。
CPU/GPU 创建耗时优化不作为持久场景正确性闭环的前置条件；后端能力受阻须明确记录，不能虚报 P5/P6 完成。

## 验收规则

遵循[测试指南](../guides/testing.md)：覆盖行为，不检查私有类数量或固定内部命令序列。
开发期间以编译和小型定向 CPU 测试为主；GPU/多平台统一验收安排在架构闭环后，不启动大场景或远端 CI。

- 无 World 创建模型预览；同场景多视图、不同场景独立目标，实例稳定身份与层掩码正确。
- 创建成功后立即更新；单操作接受失败无副作用；多操作部分成功时重试不重复；
  重复删除、跨场景/跨服务、旧 generation、服务关闭后的请求安全。
- 父变换、子对象删除、组件换绑、网格热重载改变包围体、隐藏再显示均正确同步。
- 一个实例/场景销毁不破坏共享资源；队列延迟及外部资源释放不产生悬空引用，在途帧不看到新 payload。
- 队满重试不重复应用；跳帧不丢状态；关闭/设备失败能收尾，浏览器不阻塞。
- 小型集成覆盖编辑、撤销、场景切换、资源异步就绪和失败降级；复用既有功能用例。
- 记录静态场景更新量、动态变换量、多视图成本、队列峰值、延迟和资源回收；不先设任意耗时门槛。

## 未决实现项与建议

总体边界、直接 API 与 Granit 私有后端划分已确认。实施需根据代码证据决定：命令容量与背压策略、
复用何种 Granit 队列/fence、资产适配具体落点、
临时数据是否复用现有 draw list，以及后端 payload 的具体保活机制。默认复用已有机制，避免双队列和双退休系统。
若必须改变已确认所有权、增加跨仓库能力或扩大非目标，先记录差异与建议，再与用户讨论。
