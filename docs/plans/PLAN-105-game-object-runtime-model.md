<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-105：Game Object 运行模型与 Prefab 编辑边界

## 状态与范围

2026-11-07：实施中，阶段 1、2 已完成，阶段 3、4 正在迁移。归属 [DEV-052](DEV-052-architecture-cleanup.md)，
决策见 [ADR-110](../decisions/ADR-110-game-object-runtime-model.md)。
承接已完成的 [PLAN-104](PLAN-104-value-document-prefab-overrides.md)，不重新打开其历史验收范围。
下列结构为目标设计；实施进度以检查项为准，不把重命名等同于运行存储已迁移。

## 目标

- 文档保存普通对象、Prefab 来源和覆盖；运行组件只保存组合后的有效数据。
- component_description_base 自有实例 UUID，删除 component_entry 的重复身份包装。
- 场景对象统一命名 game_object，明确区别于 meta::object。
- World 拥有 ECS 实体、组件及场景成员；game_object 和 scene_instance 是非拥有访问句柄。
- 场景树使用同一实体上的层级信息，删除独立节点槽位到实体的第二套生命周期。
- 文档历史与投影算法解耦；按 PLAN-106 保留增量同步，不新增通用事务服务。

## 当前问题

scene_instance_state 同时维护场景成员和 prefab_binding，后者仍持有来源、reference_，conflicts_ 已删除。
组件修改已不再生成覆盖。编辑文档已经权威化，但运行时仍承担来源查询、捕获和刷新。
World 层级已迁入实体组件，独立 scene_tree 槽位已删除；场景成员归属和旧 node_* 接口仍待迁移。
组件身份已收敛到 component_description_base，component_entry 已删除。

## 已确认模型

### 文档

- game_object_description 保留 uuid、name、局部 transform、components 和 optional prefab。
  普通对象的 components 为有效描述；prefab 非空时 components 必须为空，
  新增组件通过结构覆盖表达。不引入 scene_entry variant。
- component_description_base::uuid 表示组件实例，类型身份仍由 Meta 提供。
  普通数学值及 meta::object 不增加领域 UUID。
- 新建生成 UUID，属性修改和历史恢复保持 UUID，复制对象生成新组件 UUID。
  UUID 与所属对象地址组合定位；允许不同 Prefab 实例复用来源组件 UUID。
- 反射注册必须包含基类身份字段；集合校验 nil/重复 UUID 及当前每类型唯一约束。
  不通过 const_cast 改写共享描述，修改身份也要创建新描述。
- Scene 为森林，Prefab 有明确根。父关系最终使用明确的 object_address；
  普通对象地址为 UUID，Prefab 内对象地址为实例路径与来源 UUID。
  先完成普通/单层 Prefab 地址，嵌套组合仍是后续独立能力。

### 组合

输入文档与固定来源内容，输出完整有效对象描述、来源映射和诊断。
组合函数不访问 World、文件、历史或 GPU；来源缺失和覆盖冲突不得吞掉原记录。
组合结果是可重算的中间值，不形成第三份可编辑权威状态。

### 运行对象和场景

game_object 借用 world* 与带 generation 的 entity_id，不继承 meta::object，不持有强引用；
World 销毁后禁止再调用借用句柄，World 存活期间删除对象使旧句柄失效。
跨 World 操作必须拒绝，不能仅凭底层 EnTT 数值判断有效性。
场景卸载由 World 统一删除所属实体，句柄持有者不阻止删除。

实体数据：
- game_object_identity：场景实例内 UUID、名称、scene_instance_id。
- hierarchy_component：父、子和兄弟关联，所有修改经 World 校验。
- 层级组件保存 local；world 按查询即时派生，保留现有 TRS 数学语义。
  本次不引入未经性能需求验证的 world/dirty 缓存，避免增加第二套需要同步的变换状态。
- component_identities：组件类型到组件实例 UUID；只存身份，不复制属性。
- camera/mesh_renderer 等实际组件：有效值和资源强引用，不保存覆盖。

World 持有 scene_instance_state：来源场景 UUID、成员索引、根索引、对象 UUID 查询索引。
索引不是独立对象拥有者。scene_instance 对外仅借用 World 与受校验实例 ID。
同一场景可以实例化多次，通过 scene_instance_id 区分同名 UUID。
跨场景重新挂接首版拒绝，迁移成员归属需显式操作，不暗中改变卸载责任。
无场景成员的纯 ECS 实体仍允许存在。

### 编辑器关联

scene_document 持有不可变版本、历史、来源和诊断；
scene_projection 仅保存文档地址到实体的关联与同步状态，不复制组件内容。
Prefab 来源查询、Apply/Revert、来源刷新从文档和组合结果获得信息。
运行时 instantiate(prefab) 创建普通对象；运行时组件赋值不得自动生成覆盖。
来源热更新如继续提供，由外部协调组合/应用，不放回 game_object 或组件 setter。

## 已确认的迁移主线（2026-11-08 调整）

本节替代按旧接口逐项包装的实施顺序；历史完成项保留，不表示下列整链迁移已完成。

1. 建立 Engine 纯组合入口和共享 object_address；Editor 只适配固定来源输入。
   Runtime 首次实例化与编辑投影共用组合规则，先消除独立的展开实现。
2. 贯通完整有效对象的统一应用入口；空基线用于首次创建，已同步基线用于增量更新。
   对象地址存在则原地更新，消失则删除，新增则创建，不设置 Prefab 专用运行更新分支。
3. 调用方全部切换后，整体删除运行作者捕获、来源重载及长期 prefab_binding。
   需要来源热更新的外部协调方提供固定来源和目标结果，不由 World 反推作者内容。
4. 持久父关系使用同一 object_address，贯通反射、文件、校验、组合、复制和编辑。
   普通对象与单层 Prefab 成员均可定位；不为未发布格式维护兼容包装。
5. 集中编译和相关 CPU 回归，按存活身份、来源独立、资源释放、历史恢复与失败边界验收。

职责划分以 [ADR-110](../decisions/ADR-110-game-object-runtime-model.md) 为准。

当前第 1 项已接通：Engine 提供 `compose_scene` 与 `object_address`，编辑器只恢复冻结输入后调用，
Runtime 首次场景实例化复用组合后的有效组件。运行创建仍经过旧 `prefab_binding`，
准备阶段的依赖收集与独立 Prefab 实例化/重载也仍有旧路径；第 2、3 项没有因此完成。
后续应继续贯通通用应用及其调用方，不能把共享组合函数视为长期来源状态已删除。
验证见[共享组合记录](../records/2026-11-08-shared-scene-composition.md)。

通用应用已接通编辑器增量更新、普通场景首次实例化及编辑器重建路径：
`apply_scene` 接收前后有效对象、地址关联及显式资源引用，组件安装不再隐式请求资产；
编辑值准备回调仍在编辑器调用侧。编辑器重建即使包含 Prefab 也只建立普通运行对象，
来源地址通过实例化回执交给投影，不建立新的 `prefab_binding`。

剩余顺序保持明确：
- Runtime 含 Prefab 的首次实例化、独立实例化及来源重载仍走旧路径，需把来源组合与关联移交外部协调方。
- Runtime/IPC 逐帧来源查询已改用显式回执关联；旧重载完成时仍有一次来源反查过渡，
  需随重载回执迁移一起删除，见[检查关联记录](../records/2026-11-08-runtime-inspection-bindings.md)。
- 删除旧运行来源刷新、作者捕获及子树接口后，删除 `prefab_binding` 和成员 `persistent` 分类。
- 将持久父关系贯通为 `object_address`，然后完成整链验收。
验证记录见[通用应用](../records/2026-11-08-shared-scene-application.md)与[资源消费及投影重建](../records/2026-11-08-explicit-scene-resources.md)。

组合类型是可丢弃中间数据，不作为第二份文档；不为了减少结构数量而混合作者与运行语义。

## 文件与接口方向

- resource/scene/game_object_description.hpp：对象与组件描述的领域入口。
- resource/scene/scene_description.hpp、prefab_description.hpp：文档外壳。
- function/scene：纯组合、来源地址、实例化输入适配；移除运行实例覆盖状态。
- function/world/game_object.hpp：轻量对象访问入口。
- function/world/scene_instance.hpp：场景实例访问入口。
- function/world：实体、层级、场景归属及生命周期实现；EnTT 留在私有实现。
- editor/documents：文档、版本及编辑投影关联。

文件按实际依赖拆分，不为每个辅助结构创建类或 Service。
旧 node/scene_instance 接口在调用方迁移后删除，不永久保留同义别名。

## 实施顺序与验收

### 1：命名与文档描述入口

- [x] object_description 改为 game_object_description，迁移注册、校验、Loader、导入器、编辑器和测试。
- [x] 拆出独立描述头，Scene/Prefab 共用；保留已有 optional prefab 结构和互斥校验。
- [x] 更新当前接口参考；反射类型 GUID 与文件字段语义不因 C++ 改名改变。

### 2：组件身份归属

组件 UUID 迁移先补齐 Meta 的继承属性访问，再调整描述集合与编辑/恢复路径。

- [x] 补齐继承属性枚举、读写、捕获/恢复与 JSON 编解码；通过 upcast 访问基类字段，拒绝继承链中的属性 ID/名称冲突，不静默遗漏字段。
- [x] UUID 迁入 component_description_base，删除 component_entry。
- [x] 迁移反射基类字段、集合访问、文件、多态恢复和覆盖目标。
- [x] 保证编辑/撤销身份保持、新建/复制生成新身份，共享来源不被修改。
- [x] 更新样例与工具产物，不兼容未发布旧包装格式。

### 3：Prefab 组合与编辑边界

- [x] 组合结果使用明确文档对象地址，表达普通对象及单层 Prefab 成员的父关系。
- [x] 纯内存组合输出有效描述、来源信息和覆盖冲突，供编辑定位与层级查询共用。
- [x] 实例化回执显式交付对象地址与实体关联；文档侧维护独立投影映射，删除运行来源反查。
- [ ] 将对象地址贯穿持久文档父关系。
- [x] 编辑器查询/覆盖/保存/刷新不反向读取 prefab_binding；结构更新统一按组合对象地址增量投影。
- [x] 删除运行组件编辑中的覆盖生成；编辑器下发组合后的有效组件与变换。
- [ ] 迁移来源查询与刷新后，删除运行实例的覆盖表和冲突状态。
- [ ] 验证 Prefab 实例化后是普通对象，游戏修改组件不改写来源或文档。

#### 本批迁移顺序（2026-11-08）

1. 删除 prefab_binding 的直接覆盖执行及冲突缓存，运行更新只接收已经组合的有效描述。
2. 编辑器首次接管改为加载回执传入固定内容和实际资源身份，不再通过运行捕获还原作者数据。
3. 将来源节点增量更新迁入通用投影；移除运行层来源覆盖记录、旧刷新和作者捕获入口。

前两项已完成，见[验收记录](../records/2026-11-08-prefab-runtime-boundary.md)。
第三项中的编辑器通用投影已完成，删除专用 preserve_nodes 替换路径和刷新后的强制重建，
见[验收记录](../records/2026-11-08-generic-document-projection.md)。
运行来源记录、旧刷新和作者捕获仍待迁移，不将仍存在 reference_ 的运行绑定称为无 Prefab 状态。
运行集合的 capture_prepared、capture_prefab_header、capture_prefab_binding 已删除，
重复实例化改为复用调用方持有的输入；运行场景不再为这些捕获入口保活 Scene 源资产。
Prefab 头快照和冻结迁入编辑文档层，删除 Runtime prefab_placement；冻结仅消费显式资源引用。
运行层子树捕获/恢复已删除，快照及其辅助函数归属 Editor 文档模块，
见[子树归属记录](../records/2026-11-08-editor-subtree-ownership.md)。
capture_description 和旧来源重载仍依赖该记录，必须连同调用方迁移，不能简单移动字段。
宿主重载筛选已改读激活回执依赖，编辑器测试已区分文档捕获与运行组件查询，
见[调用方迁移记录](../records/2026-11-08-scene-source-callers.md)。

正式 Runtime 激活及来源热重载已接入共享组合/应用，IPC 直接接收对象关联回执；
手动 instantiate_prepared、运行捕获及对应测试仍使用旧路径，继续作为本阶段删除目标。
详见[Runtime 来源迁移记录](../records/2026-11-08-runtime-source-application.md)。

### 4：统一运行存储

- [x] 实现 game_object 借用句柄及跨 World/generation 校验，场景创建接入该入口。
- [x] scene_instance 改为借用句柄，场景集合及其成员记录移交 World 拥有。
- [x] 层级和变换迁入实体存储，删除独立 scene_tree 节点槽位。
- [x] World 拒绝不同场景已登记成员的重挂接，包含中间对象与移动子树绕过检查。
- [x] 场景访问统一校验线程与句柄，清理 World 已删除成员；借用句柄提供动态成员创建。
- [x] 运行修改接口统一按场景成员校验，移除 Prefab 特有的改名、重挂接、删除和相机限制。
- [ ] 移除旧运行捕获与来源重载对作者/来源成员分类的依赖。
- [ ] 迁移渲染采集、Runtime/IPC、编辑器和测试，删除旧接口。

### 5：验收与清理

- [ ] 文件往返、覆盖冲突、历史恢复、来源刷新与资源保活。
- [ ] 空句柄、跨 World、generation、重复删除、场景卸载、非法层级。
- [ ] 核查 Runtime 不依赖 Editor 文档或覆盖写入协议。
- [ ] 更新 Reference/Record，完成默认构建与相关回归；不运行 GPU 大场景或远端 CI。

阶段 1 默认 Windows Clang Debug 构建与 34 项定向回归通过；依赖边界检查通过，
阶段 2 已完成继承支持和组件 UUID 归属迁移，删除 component_entry；
最终默认构建与 45 项定向回归通过。详见[阶段记录](../records/2026-11-07-game-object-model.md)。
层级实体存储已迁移；场景成员归属与 Prefab 编辑边界尚未完成。

## 非目标与代价

不同时实现嵌套 Prefab、脚本绑定、流送、通用对象 GC 或跨文件事务。
2026-11-08：历史与增量投影改由 [PLAN-106](PLAN-106-document-history-projection.md) 推进，
其新决策替代本计划原先延后精细增量同步的安排。
本次会改变内部接口与未发布格式，接受广泛调用方迁移，不修改 Granit。
层级转入 ECS 后必须保证引用失效规则和变换更新顺序，不能把可移动组件地址当稳定对象指针。
本地提交按连贯验收组进行；没有本次远端发布授权。
