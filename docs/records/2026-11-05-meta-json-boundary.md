<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Meta JSON 适配边界验收

删除 detail/json_value_codec.hpp。value 的类型注册和标签编解码收回 Meta，
标签读写函数只在 serialization.cpp 匿名命名空间中定义。

新增内部 json_serialization.hpp：资源格式包装使用借用上下文的字段方法，
IPC 使用明确的 JSON 视图适配入口。递归状态只在实现文件定义，不对外提供字段。
文件格式、校验语义、注册表绑定、引用环检测及深度限制保持不变。
具体接口以[反射参考](../reference/reflection.md)为准。

Windows Clang Debug 全量构建及 25 项相关测试通过。新增独立 Meta 注册表的 value 往返、
重复注册、冻结后注册及非法值测试；已有深度 64/65、隔离注册表、IPC、Scene/Prefab、
材质和编辑器回归通过。clang-format 与 git diff --check 检查通过。

未运行大场景、完整平台矩阵或远端 CI。本次只调整内部边界，不新增通用 JSON DOM 包装。
