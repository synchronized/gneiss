<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# 编辑文档查询收敛

## 目标与非目标

删除 scene_document 的长期节点副本，Scene/World 是场景数据唯一来源。
保留文档 URI、选择、dirty、保存状态和编辑入口；不增加通知总线、版本协议或增量缓存。
独立 Runtime 镜像用于进程间通信，不在本次删除范围。

## 已确认决策

单节点查询返回自有结果值；列表查询返回调用方持有的短期快照。
命令从 Scene 查询，不把界面快照写回场景。面板读取失败必须处理，不能沿用旧缓存。
直接修改 Scene 会被下次查询看见，但不自动标记文档 dirty；编辑操作仍通过文档入口执行。
决策见 [ADR-073](../decisions/ADR-073-scene-responsibility-ownership.md)。

## 实施与验收

1. 删除文档节点数组及手工刷新，简化创建、组件修改与选择校验。
2. 迁移命令、Gizmo、面板及测试为值查询，确保局部快照的有效期。
3. 验证直接 Scene 修改可见、旧快照独立、无效选择清除、撤销重做与保存。
4. Debug 编译及相关 CPU 回归；本次不运行大场景、GPU 或远端 CI。

## 完成状态

已删除文档节点数组、refresh_nodes 和 synchronize_nodes，迁移命令、Gizmo、面板与测试。
Windows Clang Debug 编译通过，20 项相关 CPU 回归全部通过，包括直接 Scene 修改可见、
旧快照独立、失效选择清除、Prefab 编辑、撤销重做和保存。
未执行大场景、GPU 或远端 CI；查询开销尚未进行性能评估。
