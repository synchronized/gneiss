<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Prefab 运行边界与加载文档交接

2026-11-08，Windows Clang Debug，PLAN-105 第三阶段的前置迁移。
边界决策见 [ADR-110](../decisions/ADR-110-game-object-runtime-model.md)。

## 已实施

- 删除 prefab_binding::apply_override/apply_overrides；加载、重载先组合，再向运行绑定传入有效描述。
- 删除运行绑定 conflicts_ 与运行查询 conflicts。编辑器测试从 compose_scene_document 查询冲突，运行测试检查实际采用结果；失效覆盖的保留和恢复继续由组合测试覆盖。
- 有效描述校验来源 UUID、根 UUID、成员身份及层级；不匹配时拒绝修改。
- 场景激活回执携带本次固定 scene_content、来源身份及实际资源引用。取消/失败先释放这些引用，再报告清理完成。
- scene_edit_session::adopt 改为显式接收初始文档快照，删除从 capture_prepared 反推作者数据的路径。正式加载入口与测试驱动均直接交接加载输入。
- 更新现有调用方，未添加兼容重载或第二个长期文档所有者。

## 验证

- 默认 windows-clang-debug 全目标构建通过，保持 Werror。
- 场景、Prefab、组件、编辑器及 Core 边界相关 79 项 CPU 回归全部通过。
- 追加取消回执引用释放、有效描述身份不匹配测试后，场景加载、Prefab 绑定及文档快照 3 项定向回归通过。
- git diff --check 通过；未运行大场景 GPU、跨平台或远端 CI。

## 尚未迁移的边界

- scene_instance 旧来源重载仍解释 reference_；运行捕获和子树恢复仍依赖来源记录。
- 编辑投影仍借助 Prefab 绑定更新成员；下一阶段需要用通用对象差异更新代替，并一起删除旧作者捕获/来源重载入口。
- 本批不代表 PLAN-105 整体完成，未实施统一实体存储，也未修改 Granit。

## 后续：删除运行作者捕获入口

删除 scene_instances::capture_prepared、capture_prefab_header、capture_prefab_binding 及其
单实例实现。重复创建从调用方持有的不可变实例化输入和资源绑定开始；来源刷新测试也显式传入
原始或已修改的对象描述，不从运行实例重建作者数据。运行 Scene 移除仅用于旧捕获的源资产强引用。
子树内资源捕获复用普通节点的 capture_mesh_binding，不再单独维护 Prefab 索引入口。

默认 Windows Clang Debug 构建及 84 项相关回归通过；git diff --check 通过。
未运行 GPU 大场景或远端 CI。reference_、运行子树作者捕获及来源重载仍待后续迁移，计划未完成。

## 后续：Prefab 冻结回到编辑文档层

删除 Runtime prefab_placement；其 capture 无生产调用方，apply 仅保留在旧测试中。
冻结逻辑收敛为 editor::freeze_prefab_snapshot，prefab_header_snapshot 一并迁入编辑文档层。
输入为固定来源描述和显式资源映射，输出值结果；不访问 Application、World 或资产缓存。
异步编辑命令从原文档版本保活资源与本次加载结果组装临时映射，缺少引用返回 not_ready。

新增“缓存已加载但显式输入缺失仍失败”检查。测试继续验证独立来源重载不会改变冻结内容，
冻结不发布文档版本，后续撤销仍操作原编辑历史。原测试曾在历史之外直接替换运行实例，
本次删除该过时路径；文档应用仍由 Prefab 编辑、来源刷新与投影测试验证。

当前未完成边界仍是运行 Prefab reference_、文档地址到对象的独立关联与场景成员拥有权，
未将本次删除辅助入口等同于整个计划完成。

本组验证：默认 Windows Clang Debug 构建通过；84 项相关回归中 83 项直接通过，
资产编辑用例修正只读冻结边界后单独复跑通过。格式检查及 git diff --check 通过；
未运行 GPU 大场景、远端 CI 或发布。
