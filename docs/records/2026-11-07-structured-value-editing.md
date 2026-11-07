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
