<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-076：MeshRenderer 描述与实例资源编辑

状态：已完成。

## 目标与边界

- 普通节点和 Prefab 来源节点共用 MeshRenderer 反射 URI 编辑面板与 Asset Browser 替换入口。
- 复用 editor_asset_edit 的异步资源准备；成功才修改绑定、作者描述和历史，失败保留当前资源。
- Prefab 只记录 mesh_renderer_description 的稀疏 URI 覆盖，恢复源值删除覆盖；共享源资源不变。
- 加载场景收集覆盖资源，保存重开、实例刷新与撤销正确保留引用。
- 不增加第二套加载器、资源事务、GPU 进度或自动导入；不新增 Prefab 组件增删能力。

## 实施与验收

1. 补齐描述覆盖校验、资源依赖收集、实例构建与历史快照资源持有。
2. 资源编辑按普通 UUID 或 Prefab instance/source 身份定位；拒绝迟到的失效目标。
3. 接入共用反射面板与 Asset Browser，保留已有普通节点创建/移除流程。
4. 验证异步成功/失败、取消、撤销重做、无效 URI、保存重载、源数据不变和实例隔离。
5. 完整 Windows Clang Debug 编译及相关短测试；不运行大场景、GPU 窗口或远端 CI。

## 实施结果

- Inspector 与 Asset Browser 的普通节点/Prefab URI 编辑共用 `editor_asset_edit::request_property`，保留原异步加载器。
- 资源替换历史使用稳定作者身份重新定位，初次异步完成另校验原节点与绑定；相同引用不产生历史。
- 实例资源替换记录稀疏字符串覆盖，成功才提交作者数据；源值恢复自动删除覆盖。
- 纯描述覆盖处理收敛到 `prefab_property_override`，加载准备按最终生效描述收集依赖，构建直接消费有效描述。
- Prefab 刷新快照强持有覆盖资源；保持现有逐实例刷新、部分失败语义，不增加统一发布事务。
- 使用说明见 [编辑组件描述](../guides/component-editor.md)。

## 验收记录

2026-10-30：完整 Windows Clang Debug 构建通过；相关回归 19/19 通过。
覆盖协作调度和线程池下的异步成功/失败、取消、迟到结果拒绝、网格及材质 URI 编辑、无效 URI、
无操作不记历史、普通节点及 Prefab 撤销重做、根与子节点覆盖、实例隔离、源数据不变、
刷新快照与稳定身份、独立 Application 从保存文件重载、最终有效依赖收集、无窗口面板及分层检查。
未运行真实 GPU 窗口交互、大场景、浏览器或远端 CI。
