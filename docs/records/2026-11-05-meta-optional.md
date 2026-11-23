<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 可选值反射恢复验收

用户确认恢复 optional 反射，为可选描述保持值所有权。恢复独立类别、操作表和视图，
接回类型查询及 JSON 编解码。补充非标量 setter 的可写视图保护，拒绝有值但编码为 null
的歧义状态，并更新内部头文件白名单。

Windows Clang Debug：Engine、Meta 测试与编辑器属性编辑相关目标编译通过。
meta-container、meta-instance、meta-type-ops、reflection-json、editor.author-property-edit
共 5 项测试通过（1.12 秒）。新增断言覆盖空槽、ensure/reset、值复制独立性、
缺失字段默认值、显式 null、嵌套对象校验、容器往返、setter 保护及 nil GUID 歧义。

未运行大场景、全测试矩阵或远端 CI。此次不包括 Prefab 字段注册和编辑器 optional 控件。
当前接口见 [反射参考](../reference/reflection.md#可选值反射)。
