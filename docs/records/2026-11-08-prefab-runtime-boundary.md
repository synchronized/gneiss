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
