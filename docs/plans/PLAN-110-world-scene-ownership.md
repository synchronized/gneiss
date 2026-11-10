<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-110：World 对象入口与场景归属收敛

## 状态与范围

2026-11-10：本轮四项清理已完成；目标 API 的进一步便利封装仍见下方差异说明。对象改名/变换入口、ECS 成员归属及场景资产服务解耦已完成；
World 已收拢槽位并删除公开场景集合，补齐成员遍历、世界矩阵与整棵子树迁移；
相机安装与应用激活已分离，编辑组件策略已迁到 Editor；旧准备模型已删除；
运行来源关联使用外部回执，公开节点业务包装已清理，保留纯 ECS 组件入口。
沿用当前特性分支，不重新实现 ECS、资产缓存或编辑历史。
本计划细化并收敛 PLAN-105/106/107 的接口边界，不将历史任务完成等同于本计划已实现。
长期决策见 [ADR-115](../decisions/ADR-115-world-scene-ownership.md)。

## 制定时现状与目标

World 已唯一拥有 ECS，world_hierarchy 借用实体上的层级组件，game_object 是非拥有句柄。
但 world 同时暴露 entity/object/node 操作；scene_instances 和 scene_instance_state 重复转发
名称、变换、层级及组件操作。scene_instance_state 仍持有 asset_manager，混合成员管理与装配。
Scene 目录同时容纳组合、实例化、运行编辑、差异和 Prefab 覆盖，理解接口需要跨越多层。

目标是运行对象只由 World 管理；普通操作通过 game_object，场景实例只承担归属/整体卸载。
异步加载、内存组合、运行装配、编辑文档与 IPC 各自有明确入口，不新增第二套权威状态。
非目标：替换 EnTT、脚本绑定、流送、GPU 上传协议、通用事务、自动修复任意缺失 Prefab、发布。

## 已确认的对象模型

| 对象 | 所有权与职责 |
| --- | --- |
| asset_manager | 在途操作、弱缓存、异步依赖调度，不管理运行实体 |
| scene/prefab | 只读描述资产，不拥有 World 或编辑历史 |
| world | ECS、运行组件、场景实例记录、父子关系与归属不变量 |
| game_object | world 指针 + entity_id 的借用句柄，无独立存储或引用计数 |
| scene_instance | world 指针 + 带 generation 的实例 ID，无第二份对象副本 |
| scene_document | 编辑数据、Prefab 引用/覆盖、保存内容，不读写 ECS |
| 编辑会话 | 文档、历史、预览 World、投影关系和异步操作协调 |

资源强引用随组件销毁释放，弱缓存不延长其寿命。World 必须长于借用句柄使用期。
删除实体使旧 game_object 失效；不新建与 entity_id 同生命周期的另一种对象 ID。
描述 UUID 只在其描述/实例地址域中唯一，不能拿它直接充当全局运行身份。

### 2026-11-10 确认的目标 API

以下定义替代早期示意。名称与错误类型在实施时遵循现有基础类型，职责不得为迁就旧调用而改变。

```cpp
class world {
public:
  result<scene_instance> create_scene();
  game_object find(entity_id) const;
  scene_instance find(scene_instance_id) const;
  result<void> transfer_subtree(game_object root, scene_instance destination,
                                game_object parent = {});
  void update(float delta_time);
};
class scene_instance {
public:
  bool valid() const noexcept;
  scene_instance_id id() const noexcept;
  result<game_object> create_object(const object_options& = {}, game_object parent = {}) const;
  object_range objects() const;
  object_range roots() const;
  result<void> unload() const;
};
class game_object {
public:
  bool valid() const noexcept;
  entity_id id() const noexcept;
  scene_instance scene() const;
  std::string_view name() const;
  result<void> set_name(std::string_view) const;
  game_object parent() const;
  object_range children() const;
  result<void> reparent(game_object) const;
  math::transform local_transform() const;
  result<void> set_local_transform(const math::transform&) const;
  math::matrix4 world_matrix() const;
  template<class T> T* try_component() const;
  template<class T, class... Args> result<void> add_component(Args&&...) const;
  template<class T> result<void> remove_component() const;
  result<void> destroy() const;
};
```

World 唯一拥有 ECS 和场景槽位；scene_instances 收入私有实现，删除独立堆对象
scene_instance_state 层。GameObject/SceneInstance 只保存 World 指针及带代次的 ID。
普通对象的名称、层级、归属和局部变换由受控私有数据维护，不允许通用组件入口任意修改。
简单查询要求有效句柄和所属线程；可失败修改用 result。World 销毁后不能再调用借用句柄的 valid。
范围是薄遍历接口，结构修改后重新获取，不建立新容器框架。

每实体同一 C++ 组件最多一份；组件强引用正常析构。普通组件借用遵循 ECS 失效规则，
需要脏标记的写操作提供专门入口。世界变换以矩阵表达，避免将含剪切的组合强行分解成 TRS。
纯 ECS 辅助实体可没有对象层级，但必须明确归属和回收责任；场景生成的辅助实体随场景卸载。

运行对象不强制保存描述 UUID；来源查找使用本次实例化的来源地址映射。
描述对象/组件 UUID 用于文件和编辑历史，entity_id/scene_instance_id 表示运行寿命。
不增加同寿命的 instance_id；嵌套 Prefab 来源地址必须表达完整实例路径。

### 场景归属

对象实体持有 scene_membership { scene_instance_id scene; }，World 管理实例槽位及 generation。
初期通过 membership 遍历成员，不另外持有可变成员列表。以后有性能证据再加内部派生索引。
父子必须同场景；普通 reparent 不改归属。跨场景移动用显式整棵子树迁移操作，事前校验目标，
不公开 membership 的任意写入口。场景卸载释放全部成员，不能连带删除其他场景。
迁移期间无归属的旧对象入口必须明确限定，最终普通 game_object 创建都进入一个场景实例。

## 描述、加载与装配

四项已确认决定：完整重建允许场景句柄变化；实例化必须指定目标场景；运行对象不强制保留
来源 UUID；装配不激活相机或场景。禁止为保留旧行为新增 replace_members 或通用发布协议。

```text
asset_manager 异步取得 Scene 和必需 Prefab 固定内容
  → compose：纯内存展开 Prefab、应用覆盖
  → instantiate：向指定场景创建对象、安装组件和解析引用
  → 应用在安全点决定更新/渲染范围及相机
```

compose_scene / compose_prefab 输入描述及已加载 prefab_sources 借用视图，输出 composed_scene。
必需来源缺失返回错误，不发起加载。组合对象只保留来源地址、父索引、名称、局部变换和完整组件。
组合报告携带冲突；Inspector 专用来源变换、URI、覆盖列表不进入运行对象。
scene_instantiation_data 收敛掉：加载统计归加载结果，依赖归资产内容，完整对象归 composed_scene。
组合内容固定，装配不能修改共享描述。Scene 可多根，Prefab 唯一根。

```cpp
struct instantiation_result {
  std::vector<game_object> roots;
  std::vector<object_binding> objects; // 来源地址 → 本次创建对象
  std::vector<scene_issue> issues;
};
result<instantiation_result, scene_diagnostic> instantiate(
    scene_instance destination, const composed_scene&, asset_manager&,
    game_object parent = {});
```

新场景由调用方 create_scene；失败由调用方卸载新建空场景。Prefab 使用相同入口进入已有场景。
顺序：预校验 → 创建全部对象和映射 → 连接父子/设置基础数据 → 安装组件及解析引用 → 返回回执。
parent 必须属于目标场景，组合根挂在它下面。结构或必需组件失败只清理本次新对象，
不清空目标原有对象，不回滚共享资产加载。可选组件及视觉资源按明确规则降级并诊断。
视觉引用可以先存在，内容异步就绪；不等待文件或 GPU。

内部 component_build_context 借用目标对象、asset_manager 和本次 object_resolver，
不再硬编码 mesh_renderer 字段；组件自己解析所需资源。对象解析器只查映射，
无明确实例绑定的外部对象引用报告未解析，不能按 UUID 随意选择实例。
运行注册仅包含装配/更新/解析所需能力；Inspector 路由、默认编辑值、差异、历史和编辑状态策略
迁到编辑器注册，可共享转换函数，不新增运行状态副本。

相机安装只写组件，不设置全局活动相机。默认相机标记作为应用/视口选择提示。
应用负责激活、更新/渲染范围及旧场景关闭；不强制一场景一 World。
scene_domain 若只包装 World 则删除；Play 等独立运行会话确需隔离时才创建另一 World。

## 编辑器与 IPC

复用已有文档历史：文档变更 → 历史记录 → 更新文档 → 增量同步 World。
属性操作不重建无关实体；删除保存描述子树，撤销重建该子树，entity_id 可以变化。
选择/历史使用文档地址，投影负责地址到运行句柄。预览失败保留已接受文档并标记不同步、允许重试。
Runtime 不建立编辑文档。IPC 调用已有应用操作，不增加对象模型、历史或修改规则。
完整重建由编辑会话创建新实例并校验映射，再更新会话场景/投影并卸载旧实例。
场景 ID 和重建实体 ID 允许变化，选择/历史使用文档地址重新定位。
过渡共存实例只能在安全点切换，不能意外参与更新或渲染；不向 World 增加候选发布协议。

## 目标文件布局

- function/world：world、game_object、scene_instance、组件及私有 ECS/层级实现。
- function/scene：组合、实例化、组件装配注册；Prefab 组合按职责保留。
- resource/scene：Scene/Prefab 及对象/组件描述、格式校验。
- editor/documents：保留现有文档/历史/投影组织，先收敛职责再决定必要文件移动。

不以移动或合并文件数验收，不长期保留旧接口兼容包装。

## 实施与验收

按以下顺序推进，未完成项不可描述为现有能力：

1. [x] 取消保句柄替换协议；编辑会话完整重建后更新自己的场景句柄、投影和选择。
2. [x] 组合与装配分开；以 composed_scene 为唯一完整输入，实现显式目标场景和 parent 的 instantiate。
   已覆盖已有非空场景、Prefab 重复实例化、引用前向解析和仅清理本次创建对象。
   Runtime、Editor 和测试均直接消费固定内容；旧准备类型、实现和构建登记已删除。
3. [x] World 收拢场景槽位，移除公开集合及独立状态层；统一 GameObject 创建/层级入口。
   已删除公开集合及独立状态层；公开创建使用 create_object，层级/名称/销毁使用 game_object。
4. [x] 迁移运行来源身份到外部回执，补齐对象遍历和同 World 整棵子树迁移。
   已完成 visit_objects、game_object::scene/world_matrix、transfer_subtree；来源查找使用实例化回执。
5. [x] 拆分运行组件装配与编辑器注册，消除上下文中的网格特例。
   Inspector 来源信息及组件编辑策略均归 Editor；Engine 保留运行装配和通用属性读写。
6. [x] 分离相机安装和激活；迁移应用/视口安全点调度，整理 scene_domain。
   已删除 scene_domain；相机组件只保存默认提示，运行应用 activate 在切换前显式选择相机，
   编辑视口继续使用独立相机。多个提示按组合顺序选择最后一个，无提示则不自动激活。
7. [x] 清理旧 node/entity 业务入口、重复准备模型及目录；完成编译与定向 CPU 验证。

保句柄替换协议及准备模型已删除；测试目录的 instantiate_scene 辅助只组合现有接口，
不进入产品 API，不维护独立准备模型或加载预算。

关键验收：

- 同一资产两次实例化有独立实体身份；跨 World 和失效句柄拒绝。
- 跨场景 reparent 拒绝；显式子树迁移与场景卸载不破坏其他场景。
- 子树删除析构组件并释放资源引用，空句柄和重复卸载语义明确。
- 改名/变换保留对象身份；文档编辑与撤销不重建无关对象。
- 新组件不增加 World/场景集合/IPC 类型分支；组件资源异步就绪。
- 缺失/循环 Prefab 不死锁；主线程没有等待异步加载；结构与视觉就绪分开。
- 构建与必要 CPU 回归，不运行 GPU、大场景或耗时 CI；平台未验证项明确记录。

## 风险与待落实细节

没有等待用户拍板的产品问题。借用失效、组件存储变化及共享描述的可变性必须在迁移时测试。
归属迁移必须先建立不变量再移除旧校验；只搬文件或加转发不能算完成。
不先引入通用 children_view/objects_view 框架，按实际遍历与失效约束选用轻量视图。

## 首批实施记录

已删除 scene_instances 与 scene_instance_state 的 set_node_name/set_node_transform，
运行调用方使用 game_object::set_name/set_local_transform。GameObject 实现独立到 game_object.cpp，
身份和变换直接访问 World 私有层级存储，不再经场景集合转发。场景增量应用与相关测试已迁移。
父子操作暂保留 World 既有跨场景校验，待 membership 与归属操作一起替换。

Windows Clang Debug：Editor、Runtime 和相关测试目标编译通过；component-adapter、scene_instance、
scene_prefab_instance、scene-load-application、editor.session 共 5 项 CPU 测试通过。
覆盖改名保留 UUID、变换保留 entity_id、跨线程拒绝、销毁后句柄失效及文档不受运行编辑影响。
格式检查与 git diff --check 通过；未运行 GPU、大场景、远端 CI 或跨平台矩阵。
当时第一阶段其余入口和第二至第五阶段未完成，下一步集中迁移场景归属与层级校验。

## 第二批实施记录：统一成员归属

已删除 scene_instance_state 的独立成员列表、失效成员清理和集合层归属扫描。
World 的 ECS 使用 scene_membership 保存归属；创建子对象继承父节点归属，
reparent 在层级入口统一检查归属。查询与卸载直接消费 ECS，临时枚举结果不形成第二份权威状态。
替换场景时交换新旧实体的归属，再释放旧实例，保持外部场景句柄有效。
当前行为及枚举失效约束见[场景实例参考](../reference/scene-instance.md)。

Windows Clang Debug：Editor、Runtime 和相关 CPU 测试目标编译通过；component-adapter、
scene_instance、scene_prefab_instance、scene-load-application、editor.session、scene-instantiation、
world_state、scene_tree_transform 共 8 项测试通过。覆盖子对象继承归属、跨场景拒绝、
底层删除后成员查询、场景替换保留句柄及独立实例卸载隔离。格式检查与 git diff --check 通过。
未运行 GPU、大场景、远端 CI 或跨平台矩阵。

第二阶段尚未全部完成：槽位仍由 World 拥有的 scene_instances 管理，显式子树迁移接口尚未实现。
场景状态仍承担组件装配并借用 asset_manager，需在后续阶段移除；本批不表示整体计划完成。
当前成员查询采用扫描，按序号查询会构造临时枚举；后续对象入口收敛时统一批量遍历，
避免调用方重复扫描，不重新引入长期成员副本。

## 第三批实施记录：移出资源解析与组件操作

scene_instance_state 和 scene_instances 已移除 asset_manager 引用；attach 与 scene_domain 初始化
只接收运行对象所需参数。普通节点直接通过 World 创建，不再构造描述和 staged_object 绕行装配。
销毁和卸载只清理实体，资源引用随组件析构，不再清理资产缓存。

显式 instantiate_scene(world, assets, source) 负责视觉资源请求和实例化衔接，
资产管理器仅在调用期间借用。按 URI 创建网格对象、更新网格及绑定引用也移到显式装配入口。
场景集合的相机、网格、通用组件编辑转发已删除；增量应用直接使用现有组件适配操作。
scene_instance_edit.cpp 删除，未创建新的服务或长期状态对象。
Runtime、场景增量应用及测试调用已迁移；当前接口见[场景实例参考](../reference/scene-instance.md)。

Windows Clang Debug：Editor、Runtime 及相关测试目标编译通过；10 项 CPU 测试通过：
component-adapter、prefab-runtime-application、scene-instantiation、scene_instance、
scene_prefab_instance、scene_structural_reload、scene-load-application、texture-load-application、
editor.component-lifecycle、editor.session。纯组件和 Prefab 应用夹具不再构造资产或文件服务。
格式检查与 git diff --check 通过；未运行 GPU、大场景、远端 CI 或跨平台矩阵。

本批完成装配边界中对资产服务的长期借用清理，不表示第三阶段及整体计划全部完成。
instantiate_prepared / replace_prepared 仍在场景集合，仍需收敛为独立实例化与槽位替换边界；
层级操作的旧 node 接口、显式子树跨场景迁移和最终目录整理仍未完成。


## 2026-11-10 修订后首批实施

删除 scene_instances 的 instantiate_prepared / replace_prepared，已准备输入暂由
scene_instantiation 的自由函数消费；移除 swap_scene_members，不新增 replace_members 协议。
编辑器完整投影在新实例和映射校验成功后释放旧实例，更新会话的 scene_handle。
失败保留旧实例；普通增量更新继续保留存活实体。异步编辑请求已有场景身份校验，
旧实例上的未完成操作在重建后拒绝继续应用。

Editor、Runtime 及定向测试目标通过 Windows Clang Debug 编译；组件适配、场景实例化、
编辑会话、编辑组件生命周期/编辑/Prefab/属性、Prefab 实例、结构重载及运行场景检查共 10 项 CPU 测试通过。
新增断言覆盖失败保留旧场景、完整重建使旧场景失效，以及独立实例不受卸载影响。
未运行 GPU、大场景、远端 CI 或其他平台验证。

下一步为显式目标场景的 additive instantiate：不能直接将当前 apply_scene 当作追加接口，
因为它按整场景目标执行增删同步；需独立追踪本次创建对象并仅清理这些对象。
World 场景槽位、来源身份外移、组件注册拆分和相机激活分离均尚未完成。


## 2026-11-10 显式目标场景装配

新增 instantiate(scene_instance, const composed_scene&, const scene_resource_bindings&, game_object parent)。
入口不做组合或 IO，先验证地址与层级，再创建全部对象、连接父子、安装组件。
回执包含本次根对象及地址关联；同一组合结果可以重复追加到非空场景。
失败只清理本次创建对象，不卸载目标场景或删除已有对象；相机安装仍沿用当前行为，
失败恢复原活动相机，安装与激活分离仍待第六项。

编辑器完整投影直接 compose_scene_document 后调用显式目标装配，不再绕行
make_scene_document_inputs / prepare_loaded_scene。运行加载的旧 instantiate_scene
仍负责创建目标并调用同一装配入口，待统一调用方时删除过渡包装。

当前装配使用调用方提供的资源绑定，确保编辑器固定版本引用不被缓存替换；
asset_manager 参数及通用组件依赖上下文待组件装配边界收敛时落实。
运行 UUID 查询尚未移除：首次普通对象保留原查询语义，重复实例生成不同运行 UUID，
文档地址始终由每次回执映射。通用组件间前向引用解析尚未实现，第二项不标记完成。

Windows Clang Debug 的 Editor、Runtime 和相关测试目标编译通过，10 项定向 CPU 测试通过。
新增覆盖重复追加、子先父后排列、已有父节点、失败清理、循环层级、无效目标和跨场景父节点。
未运行 GPU、大场景、其他平台或远端 CI。


## 2026-11-10 组件上下文与运行入口收敛

component_context 删除网格专属 renderer 字段，改为 lookup_resource 回调；固定文档引用
优先且不回退缓存。实例化不再识别 mesh_renderer_description 来准备专属上下文，
类型解释回到网格适配器。删除无调用方的 resolve_component_renderer / prepared_component_renderer。
旧网格创建辅助暂通过私有固定引用适配函数连接通用上下文。

组件注册增加可选 resolve_references，实例化先安装全部组件，再通过本次地址映射解析引用。
增量投影更新后也调用解析步骤；对象地址查询缺失返回 not_found，必需/可选由适配器决定。
通过测试组件验证目标组件已存在、同一组合两次实例化不串引用及必需引用失败仅清理本次创建结果。
尚未引入通用“组件 UUID -> 任意组件”访问 API；组件适配器从解析出的对象访问自己的组件。

生产代码删除两种 instantiate_scene 包装。Runtime 加载协调方显式组合、创建目标、调用 instantiate，
测试便利创建移入 tests/support。scene_instantiation_data 仍用于加载阶段的依赖清单，
尚未彻底删除；component_context 的 World/node 目标表示及运行/编辑注册拆分仍待后续迁移。
第 2、5、7 项因此尚未整体验收，不将本批描述为全部计划完成。

Windows Clang Debug：Editor、Runtime 和相关目标编译通过；10 项场景/组件/编辑 CPU 测试，
以及 scene-load-application、prefab-runtime-application 共 12 项通过。
未运行 GPU、大场景、其他平台或远端 CI。git diff --check 通过。


## 2026-11-10 组件目标统一为 GameObject

component_context 删除 owner/node 字段，仅保存 game_object target。
装配及增量投影直接传已有对象句柄；旧节点入口先通过 World 查询对象并传播失败，
不添加保留旧构造形式的兼容接口。内建与测试组件从 target 获取实体，
操作入口统一校验线程及对象生命周期后才调用适配器。

空目标和卸载后的借用目标返回 invalid_handle，其他线程返回 invalid_state；
新增定向断言覆盖上述边界。World 必须比借用目标活得更久，句柄不延长对象寿命。
组件 capture/remove 等旧适配回调仍使用 World/node 参数，和运行/编辑注册拆分一起收敛；
本批仅完成上下文目标统一，未宣称整个注册 API 已统一。

Windows Clang Debug 的 Editor、Runtime 及相关目标编译通过，13 项 CPU 测试通过，
包含组件反射、适配、实例化、编辑生命周期和 Runtime 场景/Prefab 加载。
格式化与 git diff --check 通过；未运行 GPU、大场景、其他平台或远端 CI。


## 2026-11-10 组件回调目标统一

component_adapter 与 component_operations<T> 的 capture/remove/prepare_editor 改为接收
借用 game_object，内建相机/网格及测试扩展全部迁移。安装、更新和引用解析仍通过
component_context.target 获取同一对象语义。旧节点查询入口先解析并校验对象，再调用适配器；
保留的 capture_components/read_runtime_component 等 World/node 入口尚待调用方收敛。

运行/编辑注册拆分尚未实施，后续重点是解除两个行为耦合：注册运行组件不应强制提供编辑操作，
运行组件移除不应取决于 Inspector 的 removable 策略。组件差异比较和依赖收集仍属于描述语义，
不应简单与所有编辑函数一并迁到 UI 层；应先明确操作边界，再移动代码。

Windows Clang Debug：Editor、Runtime 及相关目标编译通过，13 项定向 CPU 测试通过，
覆盖组件捕获、移除、编辑准备、历史恢复及运行加载。git diff --check 通过。
未运行 GPU、大场景、其他平台或远端 CI；本批不标记 PLAN-110 完成。


## 2026-11-10 运行注册与编辑策略解耦

register_scene_component 不再强制 read/edit/modify 或独立 property_type，缺省属性身份使用
描述类型身份；缺失编辑能力统一返回 unsupported。描述校验、安装、捕获、移除和比较仍为必需，
无 dependencies 回调表示没有外部依赖。类型化注册仍生成现有反射编辑便利函数，未改变调用者行为。

remove_component 不再受 Inspector removable 标记限制。编辑命令保持原有检查，
scene_edit_session::remove_component_description 也显式检查该策略，避免直接文档调用绕过限制。
历史和投影的底层组件同步不受编辑器删除策略限制。

新增仅运行组件测试，覆盖最小注册、重复注册拒绝、无编辑操作返回 unsupported、无外部依赖、
运行安装/移除/重复移除；编辑测试覆盖不可删除组件的文档入口拒绝且原组件保留。
Windows Clang Debug 的 Editor、Runtime 及相关目标编译通过，13 项 CPU 测试通过。
格式化及 git diff --check 通过；未运行 GPU、大场景、其他平台或远端 CI。

目前运行与编辑能力仍存于同一注册记录，完成的是行为解耦，并未完成注册类型及文件拆分。
加载准备模型、World 场景槽位和运行来源身份迁移仍未完成，PLAN-110 保持进行中。


## 2026-11-10 运行与编辑注册结构分离

新增 component_editor_adapter，集中保存属性身份、read/edit/modify、默认创建及 Inspector 策略。
component_adapter 保留运行与描述操作，并以 editor 成员关联编辑扩展；仍使用一个类型索引，
不引入重复冻结状态或独立注册顺序。描述校验、比较和资源依赖收集留在运行/描述契约中。

类型化注册增加 component_editor_operations<T>，通过 component_operations<T>::editor
显式选择加入。省略时不生成任何编辑属性回调；显式提供时保留反射默认读写和自定义策略。
内建相机、网格及测试扩展迁移为 editor 分组，不保留旧的平铺字段兼容入口。
相关类型暂放在同一注册头文件，尚未进行运行/编辑实现文件拆分。

新增类型化注册检查覆盖编辑扩展缺省与显式启用。Editor、Runtime 及相关目标通过
Windows Clang Debug 编译，13 项 CPU 回归通过；格式化及 git diff --check 通过。
未运行 GPU、大场景、其他平台或远端 CI。
后续回到 World 场景槽位与重复状态层收敛；PLAN-110 整体仍进行中。


## 2026-11-10 删除每场景状态包装

删除 scene_instance_state 及其逐场景堆分配。当前 scene_instances 的代次槽位直接保存
scene_instance_id；成员查询、创建和删除直接操作 World/ECS，不再经过状态对象转发。
卸载及集合析构显式销毁对应 ECS 成员，保留组件析构和资源引用释放行为。

增加槽位复用回归，确认旧 ID 查询、重复卸载均失败且不影响新场景。
Editor、Runtime 编译通过，10 项场景、实例化、编辑及 Runtime CPU 测试通过；
git diff --check 通过。未运行 GPU、大场景、其他平台或远端 CI。

此步仅删除每场景状态层；scene_instances 集合和旧节点接口仍为过渡入口，
槽位迁入 World、借用句柄入口收敛及相机激活分离尚未完成，计划继续进行。


## 2026-11-10 World 直接拥有场景槽位

scene_slots 迁入 World 私有状态；创建、成员操作、卸载及关闭清理由 World 实现。
新增 world::create_scene() 返回借用 scene_instance，不再要求描述 UUID，且不依赖旧集合 attach。
借用句柄直接访问 World；运行加载和编辑器完整重建已使用新创建入口。
成员实现移入 function/world/world_scenes.cpp；旧 scene_instances 仅保留无独立状态的调用适配，
其节点查询/编辑调用方尚待迁移，暂未删除该类型。

新增无集合初始化的原生 World 检查，覆盖创建、成员归属、跨 World、跨线程和卸载失效。
Windows Clang Debug 的 Editor、Runtime 及相关测试目标编译通过，10 项 CPU 测试通过。
未运行 GPU、大场景、其他平台或远端 CI。下一步迁移旧集合查询/编辑调用并移除 attach；
对象范围接口、跨场景子树转移及相机安装/激活分离仍待完成。


## 2026-11-10 投影入口移除集合依赖

apply_scene 与 project_scene_document 删除 scene_instances 参数。前者从 World/场景 ID
借用 scene_instance，使用 contains 检查成员并通过句柄创建对象；后者的完整重建、失败清理、
旧场景卸载均使用场景句柄。Runtime 来源刷新和编辑历史调用同步迁移。
跨 World 的场景 ID 统一返回 invalid_handle，保留跨线程 invalid_state 和失配基线 false。

Prefab 增量应用测试取消 attach；原生 World 测试覆盖不初始化旧集合的投影及 contains 查询。
Editor、Runtime 及相关目标编译通过，10 项 CPU 测试通过，git diff --check 通过。
未运行 GPU、大场景、其他平台或远端 CI。
旧集合仍用于节点枚举、部分编辑查询及宿主 scenes() 入口，尚未删除，计划保持进行中。


## 2026-11-10 编辑会话脱离旧集合

scene_edit_session 的创建、接管、有效性和关闭全部使用 World/scene_instance，
删除仅用于关闭的 with_scene 转发模板及 scene_instances 头文件依赖。
adopt 校验回执成员归属，拒绝同 World 其他场景对象；is_open 同时校验场景域和代次，
外部卸载后立即失效。Runtime 热更新入口和宿主统计改为借用场景句柄。

新增跨场景接管拒绝、失败保留原会话、外部卸载失效且不影响其他场景的回归检查。
Editor、Runtime 及相关目标编译通过，6 项定向 CPU 测试通过，git diff --check 通过。
未运行 GPU、大场景、其他平台或远端 CI。
Runtime 检查器节点枚举与宿主 scenes() 入口仍待迁移，旧集合暂未删除。

## 公开集合删除与原生对象遍历验收

产品代码已删除 scene_instances、scene_domain 及 Application/World/scene_service 的 scenes()。
运行协调直接拥有 world_owner，实例化和 IPC 检视通过借用句柄操作。
visit_objects 同步遍历 ECS 归属，回调期间禁止结构修改；测试排序辅助仅位于 tests/support，
不恢复产品集合或成员缓存。transfer_subtree 保留实体身份及局部变换，失败恢复归属。

Windows Clang Debug 的 Editor、Runtime 及相关目标编译通过；10 项定向 CPU 回归通过，
包含跨场景迁移、剪切矩阵、实例化失败清理、编辑生命周期、加载及 Runtime 检视。
未运行 GPU、大场景或远端 CI；以上不代表主清单其余项已完成。

## 相机安装与激活边界验收

camera_component 保存 primary_hint，capture 读取提示而非活动相机身份；参数更新保留提示。
组件安装及通用 instantiate 不改变活动相机，删除实例化中的旧相机恢复逻辑。
Runtime 场景加载只记录默认选择，activate 在切换安全点显式选择；编辑视口仍独立管理相机。
Editor/Runtime 编译通过，6 项定向 CPU 回归通过，覆盖安装不激活、失败不改变旧相机、
Prefab 中默认相机在应用激活后选中及编辑相机隔离。未运行 GPU 或远端 CI。

## 固定内容直接组合

Runtime 初次加载和来源更新直接使用 scene_content 组合，再由 scene_dependencies 从有效组件
收集资源引用；删除生成准备数据再组合一次的产品路径。编辑文档冻结/恢复同样使用 scene_content，
校验、组合和依赖收集分开调用。Prefab 插入/刷新直接消费已加载来源，保留原有历史及选择语义。
composed_object 不再保存 Inspector 的 prefab_uri、source_transform、instance_root 和 overrides；
这些查询数据从冻结编辑文档取得。公开集合移除后，四个示例已迁移借用句柄。

剩余任务仍见主清单：运行 UUID 查询依赖、组件编辑注册归属、测试夹具引用的旧准备类型、
旧 World node/entity 业务入口及最终验收。产品路径迁移完成不等于这些定义已删除。

验证：Windows Clang Debug 全部默认构建目标编译通过（包含示例与测试程序，不执行 GPU）。
全量重链完成后，14 项定向 CPU 回归全部通过，覆盖文档快照/版本、会话、Prefab、组件、
实例化、结构刷新与 Runtime 检视。编译期间曾提前运行尚未重链的测试程序并发生崩溃；
新旧二进制统一后全部通过，后续必须先完成受影响调用方重链再执行测试。
未运行 GPU、大场景、远端 CI 或跨平台矩阵。

## World 层级和变换入口收敛

World 删除 object_destroy、node_reparent、node_set_local_transform、node_get_local_transform、
node_get_world_transform 和 node_get_parent，产品、示例及测试调用方使用 game_object。
world::find(entity_id) 构造借用句柄；操作时继续校验线程、身份、层级和场景归属。
未删除节点创建、身份/组件查询等剩余入口，不代表主清单已经完成。

Windows Clang Debug 全部默认目标编译通过；完整重链后 17 项定向 CPU 测试通过，
覆盖跨线程、跨场景、失效句柄、层级变换、实例化、编辑历史及 Runtime 检视。
未运行 GPU、大场景、远端 CI 或跨平台矩阵。

## 组件编辑策略归属验收

editor/components/component_registration 独立保存默认描述、可删除标记、面板路由及编辑采用回调。
Engine 的 component_property_operations 仅提供描述属性操作，供 Prefab 和 Runtime 检视复用；
Prefab 组件覆盖查询 component_types，不再读取编辑策略。自定义组件分别注册运行和编辑能力。
两套注册表各自在首次查询后冻结，不反向依赖 Editor，也不保存额外运行组件状态。

Windows Clang Debug 全部默认目标编译通过；14 项定向 CPU 回归通过，覆盖组件编辑/生命周期、
Prefab 覆盖、文档快照/历史、运行检视及原生装配。未运行 GPU、大场景、远端 CI 或跨平台矩阵。

## 旧准备模型删除验收

删除 scene_preparation.hpp/.cpp 及构建登记；scene_instantiation_data、scene_preparation_inputs、
scene_prepare_limits、prepare_loaded_scene、make_instantiation_data 均不再存在。
测试使用 scene_content 快照，显式 compose_scene、scene_dependencies、instantiate；
需要修改内容的测试先复制描述。资产身份和文件统计只在需要验证它们的加载测试中单独保留。
旧同步准备回调/预算断言随已删除 API 移除，异步请求取消、快照隔离、依赖与层级检查仍保留。
缺失固定 Prefab 统一采用 compose_scene 的 not_found，不再经过旧包装变成 invalid_argument。

Windows Clang Debug 全部默认目标编译通过；16 项 CPU 回归中，15 项首次通过，
实例化测试按上述缺失来源错误规则更新后单独重跑通过。未运行 GPU、大场景、远端 CI。
来源 UUID 与剩余 World 入口仍未完成，不能将本段验收当作整个计划完成。

## 本轮四项清理验收

来源定位统一使用实例化回执和调用方映射；instantiate 与 apply_scene 创建的运行对象不再
复制或生成来源 UUID，scene_instance 不提供 find(guid)。Runtime 检视从显式回执读取来源；
无来源动态对象按运行实体区分 IPC 身份，协议和编辑镜像允许其来源 UUID 为空。

World 删除公开节点身份、销毁、实体查询包装；object_create/node_create 收为私有实现。
公开对象创建统一使用 create_object，网格组件查询使用 entity_get_mesh_renderer。
纯 ECS 实体/组件操作保留，既有动态创建的显式 UUID 标签也保留，但不用于来源查找。

Windows Clang Debug 全部默认目标编译通过；完整重链后 28 项定向 CPU 回归全部通过，
包括组件注册、场景/Prefab 实例化和刷新、编辑历史、IPC 编解码及无来源对象镜像。
未运行 GPU、大场景、远端 CI 或跨平台矩阵。

### 目标 API 示意与实际入口

上方目标代码包含尚未提供的便利接口：children/roots 范围和 GameObject 模板组件访问。
当前遍历使用 visit_objects，组件使用现有组件适配器及 World 的纯 ECS 接口；
这些便利封装不能因本轮主清单打勾而视为已经实现。当前四项任务没有引入它们。
实际错误返回沿用 error/result，create/borrow 等准确签名以 Reference 和头文件为准。
