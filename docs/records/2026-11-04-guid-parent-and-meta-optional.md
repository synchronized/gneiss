<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景父节点 GUID 与 Meta optional 收敛

2026-11-04，分支 feature/0.52-architecture-cleanup。本批按用户确认的方案实施，不代表版本整体完成。

## 已完成

- object_description::parent_uuid 使用普通 guid，nil 表示没有父节点；迁移层级校验、快照、实例化、Prefab 和编辑器调用方。
- GUID 标量允许 nil；对象/场景身份仍要求非零，非零父节点仍必须存在，环与 Prefab 多根继续拒绝。
- 文件 parent 使用规范 GUID 字符串；根写全零 UUID，删除 parent:null 的读取支持。示例、测试夹具、导入器、Prefab 提取和性能脚本同步更新。
- 删除 Meta optional 类别、traits、ops、view 及序列化分支。业务内部可选值仍保留，Prefab 引用及组件增删状态由既有专用 codec 处理。
- 部分修改沿用已有属性路径/场景指令；无操作表示不修改，解除父子关系与删除对象分开，不新增 optional 三态协议。

当前规则以[场景格式](../reference/scene-format.md)和[反射容器](../reference/reflection-containers.md)为准。

## 验证

- Windows Clang Debug 全量编译通过，最后修正后增量全量构建通过。
- 32 项相关测试首轮 29 项通过；修正 Prefab 提取、动态夹具的旧 null 生成及 nil 父节点旧拒绝断言后，剩余 3 项复测全部通过。
- 覆盖 Meta/JSON、nil GUID 往返、场景/Prefab 格式、父子层级与环、导入器、实例化/重载、编辑操作及模块边界。
- 新增旧 parent:null 拒绝断言；保留非零对象身份、未知父节点、自引用/循环及 Prefab 根约束测试。
- git diff --check 通过；源码和构建清单无 optional_view/optional_ops/as_optional/type_kind::optional 残留。

本轮未运行 MSVC、Linux/GCC、Sanitizer、远端 CI、大场景或 GPU 工作流。
旧用户资产需把根节点 parent:null 改为全零 UUID 字符串；不提供兼容分支。
