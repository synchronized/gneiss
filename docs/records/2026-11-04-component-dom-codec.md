<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 组件数组 DOM 编解码收敛验收

日期：2026-11-04。分支：feature/0.52-architecture-cleanup。

Scene 与 IPC 共用 description_codec 的内部组件数组入口；Meta 解码临时数组后，
资源层统一校验非空与类型唯一，成功才返回列表。普通组件不再拆开后重建信封，
IPC 删除 JSON 字节中转。Meta 的多态类型 GUID 检查统一为规范小写形式，保留原 Scene/IPC 约束。
Prefab 覆盖指令的定位字段及信封提取仍属于业务适配，不在本批删除。

Engine、IPC、Meta 及受影响 Scene/Prefab/编辑器测试目标 Debug 编译通过；
重新构建后的 10 项相关 CTest 全通过（0.89 秒）。新增覆盖空组件、合法组件后的空项、
信封多余字段和 IPC 失败不替换旧快照。git diff --check 通过。
未执行全目标构建、静态构建、大场景或远端 CI。

object_description 的 components/prefab 字段尚未直接注册：前者需保证受约束集合整体替换，
后者涉及可空值及覆盖协议。没有暴露内部可写 vector 或重新引入 optional 反射；
后续工作记录在 [DEV-052](../plans/DEV-052-architecture-cleanup.md)。
