<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：Prefab 候选分步清理

2026-10-16，用户确认大型结构构造与回收也应分帧推进。
本轮只提供候选 Prefab 的内部取消基础，不代表 Editor 已完成接入。

`cancel_staged_step` 按实际创建顺序逆序释放节点和实体，再逐步释放描述记录，最后释放根和
来源租约。开始取消后禁止继续构造或应用覆盖；零预算不处理节点。重复构造同一槽位被拒绝，
创建中途失败也保留清理索引。共享缓存回收仍由宿主安排，不隐含在此步骤内。
预算限制的是工作项数量，不保证单个 World 操作、资源析构或来源描述释放的最大毫秒数。
尚未接入活动 World 的发布隔离；未主动推进清理的析构路径仍同步兜底。

Windows Clang Debug 构建通过，定向 CTest 4/4 通过：prefab_runtime_instance、
scene-load-builder、core-boundary、core-boundary-negative。新增测试验证逐步实体数量、
零预算、取消后拒绝构造、重复完成与其他实例不受影响。未进行 GPU、窗口、性能与跨平台验证。

后续范围及待确认的历史交互见[开发计划](../plans/DEV-051-architecture-boundaries.md)。
