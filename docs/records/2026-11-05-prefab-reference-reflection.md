<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Prefab 引用描述反射接入验收

已注册 prefab_reference_description 的 uri、overrides、component_overrides，以及
object_description.prefab 的 optional 属性。删除对象 codec 的手动 prefab 字段读写。
属性覆盖和组件增删指令继续使用专用 codec；URI、重复覆盖、结构/属性覆盖冲突统一校验。
缺失和 null 均表示无 Prefab，普通对象保存 null。编辑器嵌套模板检测同步支持该空值。

Windows Clang Debug 受影响 Engine、Scene/Prefab 和 Editor 目标编译通过。
6 项测试通过（0.42 秒）：scene_description、prefab_description、prefab_property_override、
editor.author-property-edit、editor.prefab-component、editor.prefab-authoring。
新增断言覆盖 null、反射属性访问、描述复制独立性、直接序列化往返、URI 错误位置和覆盖冲突。
既有测试覆盖覆盖键重复、值格式、组件增删和撤销/重做、Prefab 创建。

未运行大场景、完整测试矩阵或远端 CI。本批不提供 optional 编辑器控件，不修改覆盖指令格式。
当前格式见 [Scene 参考](../reference/scene-format.md)。
