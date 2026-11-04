<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：独立 Prefab 删除与历史快照

日期：2026-10-16。

## 实现

Hierarchy 的 Delete Prefab 入口使用现有 `editor_prefab_edit`，删除准备、整组退出、
分步清理及历史恢复复用放置事务。请求启动后同帧停止后续 Inspector 编辑，后续帧显示模态进度。
没有新增调度器、历史队列或长期隐藏实例。

删除前通过 Scene 实例服务捕获实际 Prefab 来源租约、作者覆盖、名称、父节点和当前根变换；
每步最多捕获 64 个来源节点的 Mesh/Material 租约。捕获以来源索引保存每节点实际版本，
不通过 URI 查询当前缓存，也不把同 URI 的不同版本合并。恢复使用该快照，实体和节点 ID 重新分配。
提交前取消保持场景和历史；提交后继续清理，沿用既有关闭排空契约。

旧同步实例同样可逐步准备发布组后退出。其来源和依赖已经加载，不要求为删除重新读取文件。
作者描述复制、拓扑排序及容器预留仍与规模相关；64 个捕获步骤不是严格的毫秒上限。

## 验证

Windows Clang Debug 构建通过。Prefab 编辑、历史、Prefab 作者操作、Scene Builder、结构重载及
Editor demo 冒烟共 6 项测试通过。编辑协调器和 Application Prefab 事务 clang-tidy 无告警。

集成测试分别运行协作模式和线程池，覆盖新异步实例与旧同步实例的删除、撤销恢复和再次删除；
覆盖捕获中取消、名称及根变换恢复、来源 Transform 覆盖及覆盖标记恢复。原始 Prefab、Mesh、
Material 文件损坏后仍能撤销删除。新节点 ID 不同，实例 UUID 和来源 UUID 保持不变。

本项不包含独立刷新或共享资产所有权迁移，未验证 Linux、静态安装消费者及大型资产耗时。

## 后续审计

手动刷新当前会更新同 URI 的全部实例，并重建节点/实体；异步迁移需要确认是否继续这一
节点 ID 规则，或增加稳定节点 ID 的增量事务。实例 UUID、作者覆盖及选择恢复应单独保证。

共享资产迁移不能只移动 `application_scene_state::cache`：目前
`scene_load_service::advance_impl` 在候选 ready 前调用 `adopt_versions`，仅因缓存属于私有域
才不会污染活动域。共享后需将选择准备和实际采用分开，保证取消 ready 候选不改变默认选择，
并维持激活提交的失败原子性。缓存、加载器、候选租约和渲染服务的关闭次序必须一并验证。
