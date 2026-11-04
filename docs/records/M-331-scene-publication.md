<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：Scene Tree 与 ECS 共用发布门

本轮将发布组移至内部 core 元数据，World 与 Scene Tree 共用同一状态。
候选节点的变换、实体及父节点普通查询不可见；只能通过对应组配置局部变换和删除候选叶节点。
候选父节点必须属于同一组；正常创建不能挂到未发布节点。可见节点有未发布后代时拒绝普通
子树删除，避免递归销毁候选。发布后原节点 ID 与实体 ID 同时可查询。

Windows Clang Debug 构建及 5 项测试通过：scene_tree_transform、world_state、render_snapshot、
core-boundary、core-boundary-negative。随后修正参数名与指针布尔转换告警并格式化后，重新构建，
前三项再次通过。新增测试覆盖跨组挂接、隐藏查询、父节点删除保护、叶节点清理及整体发布后渲染。

未接入生产 Prefab 事务：宿主必须将实体/节点创建与清理配对，作者文档、历史、主相机和镜像仍需
统一提交。尤其旧上层删除入口可能先改实体再删除节点，必须先做事务预检，不能直接启用候选。
未验证窗口、GPU、跨平台及大型场景性能；不宣称单次树操作具有严格时间上限。
