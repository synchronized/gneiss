<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-051：统一数学基础

状态：实现完成，编译与静态验收通过；功能回归按用户要求延期。决策见 [ADR-083](../decisions/ADR-083-unified-math.md)。

1. 接入锁定 GLM，建立 Core 向量、四元数、矩阵别名与必要辅助。
2. 迁移 TRS、场景、相机、法线、Gizmo 和编辑旋转，清除重复基础运算。
3. 迁移反射属性及导入数据，保持 GUID、序列化与 GPU 协议，统一边界转换。
4. 更新依赖声明、契约用例与文档，只编译和静态检查；功能、大场景与 CI 延期。

不设计通用数学后端或类型擦除框架，不修改 Granit 仓库。

## 验收

- Windows Clang Debug 的 Engine、Runtime、Editor、工具、示例及启用的测试目标全部编译通过。
- Core 分层检查、公共接口检查、修改文件格式检查及 `git diff --check` 通过。
- 独立安装目录包含 GLM 原始许可证，不安装 GLM 头文件或新增公共 SDK。
- 新增数学契约用例并编译，未执行单元测试、图形交互、大场景或 CI；旋转奇异点及 Gizmo 交互留待统一功能回归。
- 当前使用契约见 [数学参考](../reference/math.md)。

## 2026-10-27 Math 命名空间整理

将 `core/math` 的类型与函数迁入 `gneiss::engine::math`，更新 Scene/ECS、渲染、编辑器、
导入、示例、测试和反射注册；不保留旧名称别名。已有反射 GUID、展示名、字段顺序和序列化格式不变。
本轮 Windows Clang Debug 完整编译通过；10 项专项通过：math、value、camera_math、normal_math、
scene_projection_math、scene_tree_transform、component_reflection、reflection-boundary、
editor.transform-gizmo-math、editor.rotation-math。格式、Core 分层和 git diff --check 通过。
未运行大场景、图形交互、浏览器或远端 CI。
