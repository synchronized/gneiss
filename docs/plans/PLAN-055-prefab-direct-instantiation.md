<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-055：删除 Prefab 发布组与隐藏候选

## 状态

实现完成（2026-10-26）。Windows Clang Debug 编译通过，18 项相关测试通过；
覆盖同步实例化、World、渲染快照、编辑器 Prefab 编辑、撤销/重做与作者工作流。
未运行大场景、全量测试矩阵或远端 CI。
决策见 [ADR-088](../decisions/ADR-088-prefab-direct-instantiation.md)。

## 目标与非目标

删除 publication_group、prefab_candidate 及其 World/Scene 可见性和批量切换协议。
Prefab 使用已加载的资源引用同步创建，编辑器镜像跟随实际场景。
不改为同步文件 I/O，不增加流送系统，不扩展 GPU 上传状态，不改作者文件事务。

## 实施顺序

1. World 和 Scene Tree 删除隐藏、发布、退休状态与专用创建/查询接口。
2. Scene 提供同步 instantiate_prefab_ready 和直接 remove_prefab_instance。
3. Runtime 保留历史快照捕获及资源准备，删除跨帧运行候选和批量提交。
4. Editor 顺序执行修改，成功或失败后同步实际节点，保留成功操作的撤销/重做。
5. 将旧隐藏组测试替换为直接可见、线程边界、旧句柄失效和失败清理验证。

## 验收条件

- 源码与构建列表不再引用 publication_group 或 prefab_candidate。
- 同步实例化只消费已准备描述和资源强引用，不额外读文件。
- 错误线程、错误场景句柄返回错误，不修改目标。
- 成功替换使旧根失效；创建失败时输出为空，旧实例不恢复，无新建实体泄漏。
- 编辑器不继续显示已失效节点，成功操作的撤销/重做可用。
- Windows 开发配置编译通过，运行相关小夹具；不运行大场景或远端 CI。

## 待后续处理

独立的结构重载、作者文件事务和整场景加载隔离不在本轮删除范围。
同步实例化的性能优化待有实际规模需求再设计。
