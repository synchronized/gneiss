<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 对象通用反射编解码验收

object_description 删除 encode_object、decode_object 和 parse_object 专用包装。
通过 serialization + validate_object 使用通用编解码；校验 UUID 非空及 Prefab 与组件互斥。
场景保存调用同一 Meta 入口，删除重复的 Prefab 检查。层级和变换检查维持原有边界。
components 缺失时为空，保存始终输出，不再为 Prefab 引用对象省略该字段。

Windows Clang Debug 相关 Engine、Scene/Prefab、Editor 目标编译通过。
6 项测试通过（0.81 秒）：scene_description、prefab_description、prefab_property_override、
editor.author-property-edit、editor.prefab-component、editor.prefab-authoring。
新增断言验证直接反射入口的空 UUID 与组件互斥、缺失 components 默认值、错误组件类型、
普通及 Prefab 对象均保存空组件数组，以及场景缺失组件的加载。
既有回归验证场景/Prefab 往返、创建和编辑器撤销/重做。

未运行大场景、完整测试矩阵或远端 CI。覆盖指令仍保留专用 codec，本批不扩展编辑器控件。
当前格式见 [Scene 格式](../reference/scene-format.md)。
