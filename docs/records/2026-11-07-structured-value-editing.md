<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 结构化值与反射捕获阶段验收

关联 [PLAN-104](../plans/PLAN-104-value-document-prefab-overrides.md)。本记录仅覆盖值树基础及
Prefab 属性内容迁移，不代表组件 UUID、操作式 Prefab、文档历史或 World 投影已经完成。

## 已验证变更

- 增加只读共享的 record、sequence、map、optional、variant，以及资产/文档对象身份。
- 捕获与恢复走 Meta ops；支持内嵌多态、循环拒绝和外部资源身份解析，不使用 JSON 中转。
- Prefab 属性值直接保存 value，删除 property_snapshot 及其捕获/恢复入口；更新未发布格式样例。
- apply_value_edits 保持旧根，失败返回操作索引；replay_value_edits 保留独立成功项和冲突索引。
- diff_values 对稳定身份列表生成增删移动，普通列表整体替换；4 个身份的全部子集/排列组合验证通过。
- 编辑操作和结构化值使用注册的 $kind/$content 编解码，IPC 在注册后可传输结构化分支。

## 验证结果

Windows Clang Debug 全部默认目标编译通过。随后新增 IPC 测试修正为先验证无注册标量，
再初始化 Meta 验证对象与结构化值，定向重建通过。

以下 10 项测试通过：value、value_edit、serialization、serialization-archive、
render_asset_loader、prefab_property_override、scene_description、prefab_description、
editor.prefab-component、app.ipc-property-protocol。

未运行 GPU 大场景、远端 CI 或发布。反射恢复入口允许部分修改目标，调用方必须使用候选对象；
值树操作只负责结构校验，领域规则在采用候选前验证。错误属性路径、组件实例身份及文档统一仍在计划中。

## 操作式 Prefab 覆盖接入

Prefab 属性覆盖现保存 value_operation；稳定列表按身份生成增删移动，映射使用键操作，普通列表仍整体 set。
取消覆盖路径排序和重复路径拒绝，以保留多个操作的顺序语义。组件候选应用全部操作后统一校验跨字段约束。
编辑器和文件往返测试覆盖同一列表多次插入、删除、修改与恢复；更新实际场景格式样例。

Windows Clang Debug 默认目标编译通过，17 项相关测试通过。面板测试改为直接绘制目标列表，
不依赖其恰好位于组件末尾。此阶段尚未改变源重载的冲突策略、组件身份与文档历史。

## 来源冲突回放接入

加载、实例化和来源重载已使用 replay_prefab_reference；失败覆盖保留，独立操作继续应用。
实例查询返回冲突列表，编辑后重新计算，避免覆盖索引变化导致旧诊断错位。
同一组件的领域约束在合并操作后校验，失败只保留该来源组件。

Windows Clang Debug 默认目标编译通过；10 项定向测试通过：scene-load-preparation、
prefab-preparation、prefab_property_override、prefab_binding、scene_prefab_instance、
prefab_structural_reload、editor.component-edit、editor.component-lifecycle、
editor.prefab-component、editor.prefab-author-operations。实际重载覆盖目标消失后重现、
新增组件冲突、无效 URI 不进入依赖加载、稳定列表缺失锚点及独立编辑。
未运行 GPU 大场景或远端 CI。组件 UUID、嵌套 Prefab、文档权威根与历史仍未完成。


## 不可变编辑快照

场景文档和子树快照改为保存结构化 value，Prefab 源内容一并冻结。恢复从内存内容创建独立描述，
不通过源资产指针或 URI 推测历史版本。组件属性、添加/移除和 Prefab 组件结构历史不再直接保留
可变描述别名；已有覆盖操作继续共享不可变内容，外部资源仍由所属历史强持有。

有类型的 capture_value/restore_value 与视图接口共用实现。修复多态引用恢复的基类地址转换，
通过注册继承链调整地址并保留拥有者，避免把实际类型 ID 错当成引用声明类型。

验证：Windows Clang Debug 默认目标编译通过；16 项定向测试通过，包含序列化、多态恢复、组件生命周期、
Prefab 冲突刷新、场景保存及文档/子树撤销。未运行 GPU 大场景或远端 CI。
文档权威根、组件实例 UUID、嵌套 Prefab 与增量 World 投影尚未完成，仍按 PLAN-104 推进。


## 属性历史与冲突隔离

反射组件属性历史只捕获和恢复本次编辑的路径，恢复时在当前组件候选上应用，保留其他路径的独立修改。
整块组件替换仍使用完整内容。重算组件覆盖时保留未被本次变化触及的冲突，不从当前有效组件丢弃失效覆盖。

验证：默认目标编译通过，10 项相关测试通过。新增回归覆盖独立属性修改与撤销隔离，以及缺失稳定序列元素
产生冲突后，编辑同组件其他字段、撤销、重做均保留该冲突。


## 复审后的依赖边界调整

按确认方案更新 PLAN-104、ADR-109 和 DEV-052：保留 value 基础，强类型场景结构与通用属性内容分开；
编辑命令、历史版本和 Prefab 持久差异分工；先迁移文档归属，再优化 World 更新粒度。
嵌套 Prefab、精细增量投影及精确内存预算明确列为后续阶段，不记作已实现。

scene_document_snapshot 仅保留内容冻结/恢复与纯候选计算；新增有类型入口
make_scene_document_snapshot、make_scene_document_inputs。原有宿主捕获、恢复、资源等待和历史推进
移入 scene_document_edit，实现对象未增加，内容头文件不再引入文档拥有者、历史或 scene_instances。

默认 Windows Clang Debug 目标编译通过，7 项定向测试通过。新增 scene-document-snapshot 测试不创建
Application、World 或文件服务，验证候选计算、源修改隔离和无效快照拒绝。未运行 GPU 大场景或远端 CI。
组件 UUID、强类型文档记录及权威版本仍待迁移，现有反向捕获明确标为迁移期入口。


## 组件候选的引用隔离

核查后发现 component_adapter 的候选回调通过 C++ 拷贝复制描述，内嵌 ref 仍与来源共享。
新增回归在修复前失败：回调修改引用内容会改变原描述。现由入口统一通过 value 捕获/恢复创建独立候选，
覆盖 modify_component、edit_component 和 edit_runtime_component，不新增注册表或 JSON 中转。
不可捕获/恢复的描述直接返回错误，不降级为不安全浅拷贝；本改动不引入外部资产加载或解析器。

验证：默认 Windows Clang Debug 编译通过；两轮 10 项和 8 项定向测试均通过（共 15 个不同测试），
覆盖候选成功/失败引用隔离、组件适配、运行时属性编辑及 Prefab 回归。未运行大场景或远端 CI。
这项是领域版本迁移前的正确性修复，不代表组件 UUID 或权威文档版本已经实现。


## 强类型领域内容记录

新增 scene_record/prefab_record：对象身份、层级、名称、变换和 Prefab 来源显式保存；组件属性继续使用
反射捕获的不可变 value。文档快照、子树捕获/恢复和 Prefab 放置历史改用该结构，不再把整个场景保存为 value。
内容记录不依赖 Application 或 World，不保留可写描述别名；运行时资产保活仍归所属历史。

恢复时验证组件类型一致性、重复组件、Prefab URI/覆盖和场景层级，避免内存候选绕过文件解析校验。
捕获 Prefab 覆盖时拒绝包含可变对象引用的非持久值。测试覆盖只读版本隔离、非法父关系、重复组件、
类型不匹配、非法 Prefab 来源/覆盖及可变引用拒绝。

验证：Windows Clang Debug 默认目标编译通过，20 项相关测试通过，包括 5 项依赖边界检查。
未运行 GPU 大场景或远端 CI。组件实例 UUID、文档权威版本、统一版本历史和 World 同步仍未完成。


## 子树复制的内部引用重映射

将 uuid_mapping 移至领域记录头文件，新增 remap_scene_record 并接入实际子树恢复/复制入口。
复制显式对象 UUID、父关系和组件/Prefab 覆盖值里的对象引用；引用必须同时匹配源文档及复制对象。
跨文档、子树外引用、资产身份、普通 GUID 值及 Prefab 来源节点身份保持不变。
反射捕获的 object_reference_value 通过其已注册类型恢复/捕获，不硬编码字段编号。
完整映射校验拒绝 nil、重复来源、重复目标和遗漏对象；输入内容保持不可变。

验证：默认 Windows Clang Debug 编译通过，20 项定向测试通过（含 5 项边界检查）。新增纯内容测试
覆盖嵌套 sequence/map/optional/variant、父关系、覆盖值和非法映射；真实编辑器组件测试验证复制后
自引用指向副本，原组件引用保持不变。未运行 GPU 大场景或远端 CI。

此阶段不判断外部目标存在性，也未迁移组件实例 UUID。文档权威版本、统一历史及 World 同步仍待完成。


## 描述集合的安全候选更新

component_descriptions::update 原先直接复制 C++ 描述，内嵌 ref 仍共享原对象。现在通过反射值创建独立候选，
支持 void/error 回调；提交前再次冻结并验证内容，隔离回调替换进来的可写引用。失败保持原集合，
缺少组件返回 not_found，自定义 codec 无法捕获时返回 unsupported，不回退到浅拷贝。
子树复制检查主相机调整结果；原有故意构造非法描述的保存测试直接构造候选，仍验证保存边界拒绝非法值。

验证：Windows Clang Debug 默认构建通过，17 项组件、场景、Prefab、序列化和编辑器测试通过。
新增断言覆盖引用隔离、回调取消、异常、领域校验失败、回调外别名以及缺失/不支持类型。
未运行 GPU 大场景或远端 CI。此修复补齐候选不可变性前提，组件实例 UUID 和文档归属主线尚未完成。


## Prefab 组件编辑候选与 World 分离

将 scene_instance 内部的属性差异遍历和冲突保留逻辑移入 prefab_replay 的
prepare_prefab_component_edit。来源组件计算相对来源的差异，已有本地新增组件更新整项描述；
候选生成不访问 World、文件、资源服务或历史。组件增删仍是独立入口，属性编辑不能恢复已移除组件。
当前组件用于确定触及路径，来源描述用于计算持久差异；未触及冲突保留，重复修改归并，恢复来源消除差异。
scene_instance 只负责捕获运行时输入、安装组件和成功后采用候选，删除了两条重复的覆盖计算分支。

验证：Windows Clang Debug 默认构建通过，最终 14 项定向测试通过。新增测试在创建 Application/World 前
验证重复编辑、恢复来源、冲突隔离、非法值、删除状态拒绝及本地新增组件候选；既有测试覆盖实际运行时的
新增组件编辑、撤销重做、保存和来源冲突。未运行 GPU 大场景或远端 CI。
这完成候选计算的依赖分离，组件实例 UUID、文档权威版本及统一历史仍未完成。

## Prefab 变换候选复用领域差异计算

- 增加 `prepare_prefab_transform_edit`，组件与变换共用属性差异和冲突保留算法，结果统一为 `prefab_edit_candidate`。
- 删除运行时手工拼装三项变换覆盖的逻辑；实例根变换不作为来源根覆盖，纯入口同样拒绝该调用。
- 无 Application/World 测试覆盖子节点变换、恢复来源、无关失效路径保留、非法缩放与根节点拒绝。
- Windows Clang Debug 默认构建通过；14 项相关测试及编辑器 session 测试全部通过。未运行大场景、GPU 验证或远端 CI。
- 本次仅完成变换编辑候选收敛；组件 UUID、文档权威版本、版本历史与 World 投影迁移仍未完成。

## 文档恢复不等待视觉资源

- 文档恢复直接保活预留资源身份，移除加载组请求、轮询及整体加载成功门槛。
- 历史捕获优先保活 ECS 已绑定的网格/材质，即使正在加载或加载失败；自定义组件的其他依赖保留原缓存捕获规则。
- 验证材质文件缺失时首次推进即可采用描述，加载失败后仍可捕获、保存、撤销和重做。
- 默认构建及 7 项场景/编辑器定向测试通过。文档权威版本与组件 UUID 仍待迁移，本项不代表 PLAN-104 完成。

## Prefab 组件结构覆盖候选

- 将组件添加、移除和恢复来源的覆盖计算移入纯领域函数，运行时仅安装已校验的有效描述。
- 编辑候选带回有效描述和冲突列表，避免运行时再次回放；其他目标的冲突继续保留。
- 新增无 Application/World 的候选测试；默认构建和 12 项定向测试通过。

## 文档版本状态基础

- 新增 Editor 内部 `scene_document_versions`，保存不可变领域快照、有限版本历史和保存点。
  不执行 World、文件操作或命令回调；这是后续 scene_document 的内容状态成员，尚未接管现有编辑入口。
- 接受候选前执行领域校验并检查编辑基线，非法候选保留当前内容和 redo 分支；新编辑清除 redo。
  同合并键的连续操作保留初始版本，保存点阻断合并，默认历史上限 256 条。
- 保存回执对应实际写出的版本；保存期间的新修改继续标脏。重置文档使旧回执失效。
  保存点和已同步标记使用弱引用，不额外保活已离开历史的整份内容。
- 投影结果单独接收，失败不回退版本，过期结果不误标新版本；撤销重做后也需明确重新同步。
- Windows Clang Debug 默认构建和 8 项定向测试通过；新增测试无需 Application/World，覆盖结构错误、
  版本隔离、投影失败后序列化与重试、合并、保存点、分支、容量、过期和跨文档回执。
- 现有 scene_document 的保存、编辑及宿主命令仍走原路径。PLAN-104 的文档接管、UUID 和统一历史迁移
  未完成，本次不把基础类型存在视作编辑器行为已切换。未运行 GPU 大场景或远端 CI。

## 文档接管编辑、保存与查询

- scene_document 接管不可变编辑版本；属性、组件生命周期、层级、子树和 Prefab 编辑先校验并接受内容，再更新运行时。
- 保存、内容快照和编辑器查询读取文档，只有 adopt 初次接管实例时从运行时导入内容。World 旁路修改不再进入保存文件。
- 组件安装失败保留新版本和保存能力，单独报告投影错误；增加完整同步重试和编辑器重试入口。完整同步保留场景实例句柄并按作者身份恢复选择。
- 迁移历史捕获和测试的旁路设置；当前宿主历史仍是命令历史，组件 UUID 和统一版本撤销尚待完成。
- Windows Clang Debug 默认构建、10 项定向回归以及全部 57 项编辑器回归通过，git diff --check 通过。未运行耗时远端 CI。
