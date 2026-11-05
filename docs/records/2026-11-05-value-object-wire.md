<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Value 对象内容与 IPC 值统一验收

对象 value 以类型 GUID 和内容编码，空引用为 null。解码前确认注册类型是 meta::object
派生类，解码后核对动态身份；恢复独立对象，不保留内存共享关系。
复用引用对象构造、解码和校验路径，以及原有递归上下文和引用环检测。

删除 ipc_property_value、ipc_property_payload、runtime_property_value，IPC、Host 和
Editor 模型直接使用 engine::value；删除 Runtime 双向值转换与 Editor 的重复转换。
保留 IPC 既有标量标签、字段及大小限制，补充 vec2/vec4/object 标签。
对象 IPC 依赖初始化并冻结的默认 Meta 表，标量 IPC 保持原有初始化要求。
实际格式和限制见[通用值](../reference/value.md)。

Windows Clang Debug 全量构建及 33 项相关测试通过。
新增覆盖对象/空引用 IPC 往返、接收端独立对象、重复引用独立恢复、
未知类型和非对象类型拒绝、循环引用及深度超限。既有 Meta、Scene/Prefab、
IPC 实际进程、编辑器属性与快照拒绝测试通过。
clang-format 和 git diff --check 通过。

未运行大场景、跨平台完整矩阵或远端 CI。未开放所有 Runtime 组件的对象属性写入；
编辑器对象替换 UI、资源 URI 解析、远端对象 ID 与撤销快照语义未改变。
