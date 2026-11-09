<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-110：World 对象入口与场景归属收敛

## 状态与范围

2026-11-09：实施中。首批对象改名/变换入口迁移已完成；membership、层级、组件与装配仍待实施。
沿用当前特性分支，不重新实现 ECS、资产缓存或编辑历史。
本计划细化并收敛 PLAN-105/106/107 的接口边界，不将历史任务完成等同于本计划已实现。
长期决策见 [ADR-115](../decisions/ADR-115-world-scene-ownership.md)。

## 现状与目标

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

### 目标 API（示意，实施时复用现有命名及结果类型）

```cpp
class world {
public:
    scene_instance create_scene();
    game_object create_object(scene_instance, const object_options& = {});
    game_object find(entity_id) const;
    void update(float delta_time);
};
class game_object {
public:
    bool valid() const noexcept;
    entity_id id() const noexcept;
    result<void> set_name(std::string_view);
    game_object parent() const;
    children_view children() const;
    result<void> reparent(game_object);
    math::transform local_transform() const;
    result<void> set_local_transform(const math::transform&);
    template<class T> T* component() const;
    template<class T, class... Args> result<void> add_component(Args&&...);
    template<class T> bool remove_component();
    void destroy();
};
class scene_instance {
public:
    objects_view objects() const;
    roots_view roots() const;
    game_object create_object(const object_options& = {});
    void unload();
};
```

简单查询使用值、指针或空句柄；需要区分失效/跨线程/非法输入时保留 error/result。
不为符合示意强行删除现有错误信息。遵循 PLAN-109，内存与意外异常不逐函数转码。
组件借用在删除/存储变化后重新获取；层级、归属及维护不变量的变换不能绕过专门接口任意改写。
纯 ECS 辅助实体不必具备对象身份和层级。

### 场景归属

对象实体持有 scene_membership { scene_instance_id scene; }，World 管理实例槽位及 generation。
初期通过 membership 遍历成员，不另外持有可变成员列表。以后有性能证据再加内部派生索引。
父子必须同场景；普通 reparent 不改归属。跨场景移动用显式整棵子树迁移操作，事前校验目标，
不公开 membership 的任意写入口。场景卸载释放全部成员，不能连带删除其他场景。
迁移期间无归属的旧对象入口必须明确限定，最终普通 game_object 创建都进入一个场景实例。

## 描述、加载与装配

复用已有 game_object_description、component_description_base 的 UUID、可选 Prefab 来源，
不为改名字复制一套结构。Scene 支持多根，Prefab 有唯一 root_uuid；nil GUID 的 JSON 使用 null。
带 Prefab 来源的描述代表实例根；放置属性属于实例，内部差异放覆盖记录，不存第二份展开子树。
运行组件只接收展开后的完整内容，不保存 Prefab 覆盖。

```text
异步加载 Scene → 发现并递归异步加载 Prefab → 结构依赖就绪
    → compose（纯内存）→ instantiate（所属线程同步创建对象）
    → 视觉资产内容可继续异步加载
```

compose 输入描述与已加载固定 Prefab 查询视图；不读文件、不阻塞、不管理加载请求。
循环依赖在调度中检测，不能形成互等。复用资产记录去重和弱缓存。
复用当前 composed_scene/实例化数据，逐步收敛为一份临时完整描述，不增加 resolved_scene 平行模型。
实例化回执给出实例、来源地址到对象的关联和诊断；普通 Runtime 不需要关联时即可释放，
热重载协调器或 Editor 按用途保留，World 不持有来源快照。

实例化先检查重复身份、非法父引用、循环；再创建对象、连接父子、装配组件和解析引用。
必需 Prefab 缺失拒绝实例化；视觉资源失败保留对象并诊断、按组件规则降级。
可选组件只有声明可跳过策略才能跳过，不能默默遗漏。失败清理本次创建对象不等于跨场景事务。

组件装配提供按描述类型注册的入口，纯数据可默认映射，资源/对象引用显式解析。
资源解析获得稳定强引用并触发异步加载，不等待内容。运行装配注册不兼任 Inspector、撤销和 IPC。
增加组件不修改 World/场景集合的类型分支；Editor 更新能力可以复用转换函数但独立注册。

## 编辑器与 IPC

复用已有文档历史：文档变更 → 历史记录 → 更新文档 → 增量同步 World。
属性操作不重建无关实体；删除保存描述子树，撤销重建该子树，entity_id 可以变化。
选择/历史使用文档地址，投影负责地址到运行句柄。预览失败保留已接受文档并标记不同步、允许重试。
Runtime 不建立编辑文档。IPC 调用已有应用操作，不增加对象模型、历史或修改规则。

## 目标文件布局

- function/world：world、game_object、scene_instance、组件及私有 ECS/层级实现。
- function/scene：组合、实例化、组件装配注册；Prefab 组合按职责保留。
- resource/scene：Scene/Prefab 及对象/组件描述、格式校验。
- editor/documents：保留现有文档/历史/投影组织，先收敛职责再决定必要文件移动。

不以移动或合并文件数验收，不长期保留旧接口兼容包装。

## 实施与验收

1. [ ] 统一对象入口：迁移名称、变换、层级、组件调用；移除重复 scene/object/node 转发。
   首批删除场景层改名和变换转发，GameObject 实现直接访问 World 内部层级，独立源文件。
2. [ ] 统一 membership：场景槽位归 World，替换 scene_instance_state 的成员列表；
   将父子关系、场景归属及跨场景移动的检查放在同一处，删除旧归属扫描路径。
3. [ ] 分离装配：异步结构依赖、纯组合、实例化与资源解析；运行场景状态不持有 asset_manager。
4. [ ] 统一组件入口与消费者：Editor、Runtime、IPC 使用运行对象/装配边界；保持文档权威。
5. [ ] 删除旧类型/入口和无职责包装，整理目录与接口文档，完成编译及定向 CPU 验证。

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
第一阶段其余入口和第二至第五阶段未完成，下一步集中迁移场景归属与层级校验。

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
