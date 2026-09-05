<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-157：Scene 事务式结构替换

## 结果

Scene 实例服务新增内部结构重载能力。宿主传入现有 Scene 句柄与作者 URI 后，服务重新解析完整候选
描述、计算稳定 UUID 差异并预取全部候选渲染资源，再在 Application 所有者线程提交普通节点变化。

- 匹配 UUID 的节点 ID 与实体 ID 保持不变，实体上的非作者组件不会因替换而重建。
- 新节点按父到子创建，重挂接发生在删除前，旧节点按子到父销毁。
- 名称、Transform、Camera 与 Mesh Renderer 仅在对应作者字段变化时覆盖。
- Scene UUID 或 Prefab 实例容器变化会被拒绝，Prefab 事务由 M-158 单独实现。
- 候选解析或资源预取失败不会修改旧结构；提交阶段的可恢复更新保存快照并回滚。

该能力只通过 `application_internal::reload_scene` 暴露给仓库内宿主，不扩展当前稳定 C ABI；Runtime
IPC 接线留给 M-159。

## 验证

- Windows Clang Debug 完整构建通过。
- 127 项 Windows Clang Debug 测试全部通过。
- 新增 `gneiss.scene_structural_reload`，覆盖身份保持、普通节点增删和资源准备失败后旧结构保留。
