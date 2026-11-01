<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-075：统一 Transform 属性编辑

状态：已完成。

目标：普通节点、Prefab 根和来源子节点共用反射属性树及描述编辑命令；复用 scene_document 的根实例变换/来源覆盖规则，删除无生产调用的作者平面 Inspector 模型与控件。运行时 World 反射接口保留。

验收：完整 Debug 编译，拖动合并、选择变化后撤销、无效缩放拒绝、Prefab 根与子节点差异及保存重载；无窗口面板测试。不运行大场景或远端 CI。

## 实施结果

- Transform 与 Camera 复用 `edit_component_description` 的属性路径解析、候选校验和历史提交；Transform 仍保存为对象的 transform 字段，不改变文件 Schema。
- 作者 Inspector 的普通节点、Prefab 根和来源子节点使用同一反射树，旋转仍通过欧拉角显示。
- Prefab 恢复单字段及恢复全部变换使用同一命令入口；恢复全部为单次整体赋值，可撤销。
- 删除旧 `edit_author_property`、作者平面属性快照模型、旧面板及仅针对已删除模型的测试；有效行为覆盖迁移到描述命令及共用面板测试，边界检查更新至实际入口。
- 运行时 World 反射属性 API 和 Runtime 调试面板保留，不混同作者文档历史。

## 验收记录

2026-10-30：完整 Windows Clang Debug 构建通过，相关回归测试 19/19 通过。
覆盖同次拖动合并、不同操作分组、选择变化后撤销、容量失败不修改、零缩放拒绝、Prefab 根变换不生成覆盖、来源子节点覆盖及恢复、源资源不变、保存后重建及共用面板无窗口绘制。
未运行真实 GPU 窗口交互、大场景、浏览器或远端 CI。
