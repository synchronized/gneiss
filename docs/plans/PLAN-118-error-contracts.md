<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-118：错误返回与内部契约收敛

## 状态与范围

2026-11-12：P1 进行中。World::flush_changes 已改为 void，entity_count 已改为直接返回
std::uint64_t 的 const 查询；两者以全配置断言检查所属线程。Application、Runtime 统计及
Scene/Prefab/Editor 测试调用已迁移，变化合并、回调异常边界和渲染重试语义不变。
内部访问及 GameObject 查询尚未迁移，P1 未完成。

数量查询原有的错线程错误返回测试改为子进程终止测试，同时覆盖变化派发；
外部实体查找的错线程拒绝仍独立验证。删除 Editor 中只检查数量查询成功、未检查数量的断言，
其余对象数量、失败后不残留实体和失效句柄行为断言保留。

2026-11-16 验证：World 服务与错线程子进程测试通过，数量查询和变化派发的终止分支均已执行；
本轮同时通过相机数学与相机选取回归。仅验证 Windows Clang Debug 构建，未执行其他平台和完整 Release 构建。

权威规则见[代码规范](../guides/coding-style.md#返回值契约与终止的选择)。
本计划延续已完成的 [PLAN-109](PLAN-109-exception-boundaries.md) 与
[ADR-114](../decisions/ADR-114-exception-boundaries.md)，不重新实施异常捕获清理。
libassert 已接入，验证范围见[接入记录](../records/2026-11-12-libassert-integration.md)。

## 目标与非目标

目标是使返回类型表达真实失败语义：可处理的失败保留诊断，内部契约违例及时终止，
无失败来源的函数直接返回值或 void，并消除完整调用链上的无意义错误传播。
保留异步状态、资源预算、设备故障、外部数据校验与编辑器降级行为。

非目标：全仓文本替换错误码、全量删除 result、增加第二套错误框架、将未知后端状态一律 fatal、
强制所有模块引入 libassert、重写资源格式、修改 Granit、远端发布。
不以每个函数都有断言为目标，也不为统一签名单独保留永远成功的 result。

## 源码核查与候选调整

下面是待评审的 API 迁移候选，不是已经实现或逐项确认的新接口。

| 范围 | 当前问题 | 候选处理及边界 |
| --- | --- | --- |
| World 内部访问 | world_component_access::get 仅用错误返回表达线程约束，调用者重复传播 | 若确认所属线程为调用前置条件，改为断言并返回 implementation 引用 |
| World 查询/通知 | entity_count、flush_changes 传播线程错误 | 核查回调链后考虑数量直接返回、通知返回 void；不改变回调异常边界 |
| GameObject | name 直接返回，其他查询多层 result；层级缺失也返回 invalid_handle | 统一借用查询契约；内部断链与外部过期身份分开，改父循环等实际拒绝保留 |
| Meta getter/ops | 类型契约违例与数据值非法合并返回 internal | 拆开来源；已注册适配器违约才断言，未注册/可选能力缺失及数据错误保留查询或诊断 |
| Render 执行器 | 回执身份异常通过 internal 向上传播 | 先证明唯一消费、完成与关闭契约，再判断不变量；背压、设备故障与第三方结果保留错误 |
| Prefab 重放 | 按 internal/out_of_memory 名称归入 fatal 分支 | 追踪具体产生点，区分操作终止与进程终止，不直接替换该分类函数 |

## 实施顺序

- [x] P0：统一评审规则、登记候选与保护边界，链接现有异常与测试规范。
- [ ] P1：World/GameObject 完整链。先列现有调用者与失效/线程测试，再确定直接查询签名；
  迁移内部访问及 Runtime/Editor 消费者，删除被新契约取代的传播分支和测试。
- [ ] P2：Meta 内部 getter/ops。逐项拆分契约违例与输入问题，验证属性面板、序列化和覆盖应用。
- [ ] P3：Render 执行器及 Prefab 重放定向审计。回执失配必须先核实并发/关闭时序；
  保留队列拒绝、取消、预算与后端恢复行为，不扩大为渲染架构改造。
- [ ] P4：对上述范围复核残留错误传播、更新 Reference、完成相关构建与回归并关闭计划。

每阶段使用同一特性分支；每组完整且验证通过的调用链独立本地提交。
本计划完成不代表全仓所有错误码都已审计；其他模块后续按相同规则随真实改动整理。

## 每条调用链的评审模板

1. 初始化：谁创建、谁拥有，输入在哪验证，是否有可处理的启动失败。
2. 正常调用：最小调用示例，必要前置条件，返回值是否存在真正失败来源。
3. 失败：来源是外部数据、正常缺失、业务拒绝、契约违例还是后端故障；上层能采取什么动作。
4. 生命周期：删除、关闭、重载与异步回调后，是否仍有有效借用；断言不能修复悬空引用。
5. 修改结果：新旧签名、删除的传播、保留的校验、受影响测试与文档。

例如 P1 的候选调用由 `result<storage*>` 加错误传播，变为带所属线程前置条件的 `storage&`；
外部实体 ID 仍先经过可能失败的查找。调用示例必须覆盖失败与销毁，不仅展示成功路径。

## 验收

- 正常查询不再要求调用方处理仅代表编程错误的状态；合法的缺失与拒绝仍能区分。
- 非法文件/IPC/属性数据不会因为迁移变成进程退出；过期 RID 与异步生命周期保护仍有效。
- 新终止分支在 Debug/Release 均遵守契约；必要的子进程测试不弹出交互崩溃窗口。
- 移除错误分支后相关调用者实际简化，不保留平行 checked/unchecked API，除非有两个真实使用场景。
- 每阶段运行受影响构建与行为测试；不为纯文档或简单转发新增测试，验证规模遵循 PLAN-114。
- 总结区分已实施、未验证与仍保留的可恢复错误，不以 grep 命中数量作为完成证据。

## 待逐链确定的问题

GameObject 现有失效句柄操作是否被编辑器正常使用、Meta 自定义回调的数据契约，以及 Render
关闭时回执是否可能合法缺失，均须在对应阶段通过源码和测试确定。
默认保留已有可恢复语义；只有确认它实际属于编程错误后才调整。若需改变已确认的生命周期或
跨模块协议，先说明具体影响再讨论，不把推测当成不可达状态。

## P1 已确认的借用 API 契约（2026-11-17）

先调整正式 API，再迁移 PLAN-119 的测试包装，避免重复适配旧错误语义。
World 必须覆盖借用句柄的使用期；普通查询要求所属线程及有效 generation，使用断言检查。
World 已销毁后的悬空指针不能通过 is_valid 修复。外部 ID、IPC 和异步完成处理先验证身份。

| 接口 | 目标返回 | 正常缺失与失败边界 |
| --- | --- | --- |
| GameObject 的 local_transform/world_matrix/scene/parent/children | 对应值 | 根 parent 返回空句柄；其他内部层级断裂属于不变量违例 |
| SceneInstance 的 size/objects/roots | 数量或句柄快照 | 空场景返回零或空集合 |
| SceneInstance::contains | bool | 当前场景须有效；空、过期、其他 World 的对象返回 false |
| set_name/destroy/unload | void | 有效句柄、所属线程为前提；销毁后先检查 is_valid，不重复操作 |
| create_object/reparent/set_local_transform/transfer_subtree | 保留错误 | 非法变换、循环、跨场景等真实拒绝仍可恢复 |
| visit_objects | 暂保留 error | 回调存在真实失败，不能随查询错误一起删掉 |

实施示例：创建场景成功后调用 objects()/contains() 不再拆 result；接收外部 ID 时先通过
world.object 或 scene_instance::borrow 验证，再调用查询。unload 后仅在 World 存活期间用
is_valid 判断失效，不再查询已卸载场景。分批迁移完整消费者链，未迁移签名保持现状。

### P1 第一组实施结果

已迁移 SceneInstance::contains、objects、roots：直接返回 bool/句柄快照，以所有配置启用的
GNEISS_ASSERT 检查场景存活及所属线程。is_valid 直接校验槽位，不再为判断存活统计全部成员。
objects/roots 不再经过可失败回调接口生成快照；辅助 ECS 实体仍不作为 GameObject 返回。
实例化、场景更新、跨场景迁移和编辑器接入保留入口验证及业务拒绝，删除 contains 的无意义
错误拆包。测试同步验证正常缺失、删除后的归属与空快照，不新增逐接口死亡测试。
Windows Clang Debug 五目标构建通过；world-service-boundary、scene_instance、scene-instantiation、
prefab_structural_reload、editor.author-property-edit 五项回归通过。未运行 Release、静态库、
跨平台或 GPU 验证。size、GameObject 查询与修改/销毁签名尚未迁移，P1 未完成。

### P1 第二组实施结果

scene_instance::size 直接返回 uint64_t，沿用有效场景与所属线程断言。删除 world::scene_size
中间入口；borrow 直接校验槽位，不为验证外部 ID 枚举场景成员。
Runtime 重载提交/完成、检查与统计，以及编辑器投影/接入显式验证可能过期的场景，保留
可恢复错误；正常数量比较不再拆 result。旧 scene_access 夹具暂保留边界检查，待 PLAN-119
退出消费者后删除。更新 scene-instance Reference，未将 GameObject 尚未迁移的查询描述为完成。

Windows Clang Debug：Runtime 及九个相关测试目标构建通过，场景检查、加载器、结构重载、
场景加载、场景实例、Prefab 应用、World 边界、组件装配和编辑器属性编辑九项回归通过。
构建中修正 Prefab 应用测试遗留的私有包含根为 engine/src，其重复生产源码仍未清理。
未运行 Release、静态库、跨平台及 GPU 验证。P1 的 GameObject 查询和修改/销毁迁移仍待完成。

### P1 第三组实施结果

GameObject 的 scene、children、world_matrix 直接返回值；使用所有配置断言检查有效对象与
所属线程，父子层级断裂及缺失场景归属作为不变量违例。渲染同步在确认对象与组件存活后直接
使用矩阵。transfer_subtree 保留外部根对象有效性检查，过期根仍返回 invalid_handle，测试
覆盖这一边界，避免通过 scene() 断言替代可恢复失败。
子对象集合仍为非拥有句柄快照；保留删除后的失效检查及包含剪切的矩阵组合测试。
world_transform 仍由相机和编辑器使用，本组未删除或改变 TRS 组合行为；local_transform、
parent 以及修改/销毁接口尚待迁移。
Windows Clang Debug 四目标构建与 native-scene-lifecycle、scene_instance、world_render_binding、
world-service-boundary 回归通过；新增过期根检查后重跑 World 边界通过。
未运行 Release、静态库、跨平台与 GPU 验证；P1 未完成。

### P1 第四组实施结果

parent() 直接返回 GameObject，根对象返回空句柄；有效对象及所属线程为断言契约，内部父节点
失效属于层级不变量违例。场景重放与 Runtime 检查直接读取父句柄，保留调用前的外部对象查找。
Prefab 和生命周期测试删除 assign_value 输出参数包装，直接比较父句柄或实体身份；根对象
空父节点、改父失败后层级不变、重载后的父关系仍受验证。已销毁对象不再调用 parent 期待
错误码，改为保留 is_valid 失效检查。未增加平行 checked 查询或逐接口死亡测试。
Windows Clang Debug 九个目标构建通过，原生生命周期、场景实例、场景 Prefab、Prefab 应用、
组件装配、结构重载、Runtime 加载/检查及编辑器 Prefab 编辑九项回归通过。
未运行 Release、静态库、跨平台或 GPU 验证；local_transform 和修改/销毁接口尚未迁移。

### P1 第五组实施结果

local_transform() 直接返回 math::transform 副本，以有效对象与所属线程为断言契约。
反射读写先通过 world.object 验证外部实体，再读取副本；写回仍通过 set_local_transform
校验。场景重放、Runtime 检查及测试移除无意义错误拆包和输出参数包装。失效对象测试改为
检查身份查找失败或 is_valid；保留非法变换写入后原值不变、跨线程反射拒绝等边界验证。
Windows Clang Debug 十二个相关目标构建通过；World、组件反射/装配、场景实例、Runtime
加载/检查/属性编辑、Prefab 重载、属性检查器、编辑器属性编辑/Prefab 编辑/保存十二项回归通过。
未运行 Release、静态库、跨平台或 GPU 验证。world_transform 的 TRS 语义及修改/销毁接口
仍未迁移，P1 未完成；普通查询本轮不扩展为修改接口契约变更。

### P1 第六组实施结果

set_name() 改为 void，以有效对象及所属线程为断言契约；保留输入字符串复制，允许传入自身
名称的借用视图。场景创建、场景更新与编辑器调用删除无意义的成功码判断；外部身份检查
仍由既有入口承担。测试直接验证创建名称与改名结果，移除对空、已销毁或跨线程对象改名
期待错误码的断言，保留失效身份检查及其他可恢复修改失败验证。
Windows Clang Debug 八个相关目标构建通过；World 边界、原生生命周期、场景结构重载、
组件装配、场景 Prefab、场景实例、编辑器会话与保存八项回归通过。
未运行 Release、静态库、跨平台或 GPU 验证。其他修改/销毁接口及 world_transform 的
TRS 语义尚待审查，P1 未完成。

### P1 变换与层级修改实施结果

set_local_transform 与 reparent 的接收对象有效性、所属线程检查改为断言；保留非法变换、
失效父对象、跨 World、跨场景和循环层级的错误返回。内部祖先链断裂属于不变量违例。
反射属性写入、场景更新仍先验证外部实体；实例化使用刚创建的对象，不新增平行 checked API。
测试移除对已销毁对象设置变换期待错误码的旧契约；跨线程测试改为验证 world.object 边界，
补充失效父对象拒绝且原层级不变。保留变换拒绝、循环、跨场景及渲染变化通知验证。
Windows Clang Debug 八目标构建及 World 边界、原生生命周期、场景树变换、组件反射/装配、
场景实例、渲染同步、场景结构重载八项测试通过；git diff --check 通过。
未运行 Release、静态库、跨平台或 GPU 验证。销毁/卸载、组件操作及 world_transform 审查
仍未完成，P1 保持进行中。
