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
