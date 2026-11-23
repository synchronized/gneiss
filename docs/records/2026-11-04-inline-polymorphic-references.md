<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 显式多态与组件引用迁移验收

日期：2026-11-04。分支：`feature/0.52-architecture-cleanup`。
决策见 [ADR-103](../decisions/ADR-103-inline-polymorphic-references.md)，
当前接口见 [反射参考](../reference/reflection.md)。

## 已交付

- Meta object / object_type 提供实际身份；polymorphic/base 注册声明多态及安全上转型。
- 引用按值内联序列化，删除自动对象表；重复引用恢复独立对象，循环引用失败。
- 组件持有统一为 ref<const component_description_base>，删除旧包装的 encode/decode。
- Scene/Prefab、编辑器和 IPC 使用通用序列化；具体类型既可注册字段，也可使用自定义 codec。
- 公开组件操作拒绝空引用；组件快照修改仍复制具体值并替换，不修改共享旧快照。
- 两个已链接 Engine 的测试目标删除重复编译的已导出 codec，修复 Windows 导入/导出冲突。

## 验证

`cmake --build --preset windows-clang-debug -j6` 全目标构建成功。
26 项相关 CTest 全通过（2.94 秒），包括 Meta 视图/容器/默认注册表、反射 JSON、
组件描述与适配、Scene/Prefab 编解码和实例化、IPC 场景检查、编辑器组件编辑与撤销。
多态测试覆盖指针偏移、实际字段值、未知/无关类型、失败保持旧引用、null 和重复引用独立恢复。
`git diff --check` 通过。源码扫描确认旧组件 encode/decode 和 Meta 对象表协议已删除。

## 限制

本批未运行静态构建、跨平台矩阵、大场景或远端 CI，不包含版本发布。
基类只承载身份；若基类注册属性，freeze 返回 unsupported，尚未实现继承字段自动合并。
自定义 codec 自行递归调用序列化时须负责跨调用的循环保护。共享持久化身份由业务 ID 管理。
