<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Meta 与序列化类型归属整理验收

日期：2026-11-05。环境：Windows、windows-clang-debug。

## 完成范围

- 编解码诊断及绑定归入 serialization，删除 meta/serialization_types.hpp。
- 构造、基类转换和反射对象转换归入 meta::object_ops；同一注册表保存并提供查询。
- 类型分类和枚举项移入 type_types.hpp；注册输入移入 type_definition.hpp。
- 内建类型注册实现移入 meta/builtin_registration.cpp，value 编码保持在 serialization。
- JSON 节点适配收进 json::detail；前置声明统一为 ops_fwd.hpp。
- variant 分支名称定制点改为 variant_case_traits，不保留旧名称别名。
- 更新调用点、内部头清单、边界检查及当前参考文档。

当前接口见[反射参考](../reference/reflection.md)与[序列化参考](../reference/serialization.md)。
决策见 [ADR-105](../decisions/ADR-105-serialization-format-boundary.md)。

## 验证

- `cmake --build --preset windows-clang-debug -j5` 通过。
- 52 项相关回归通过，包含类型注册、反射边界、多态引用、容器、JSON、通用内存后端、
  Scene/Prefab、资源加载、IPC、编辑器与导入。
- 核心边界、核心边界负向、内部头可见性 3 项检查通过，共 55 项。
- 新增 object_ops 用例验证构造、偏移基类转换、反射对象转换和空注册表拒绝。
- 67 个修改或新增 C++ 文件的 clang-format 检查及 git diff --check 通过。

未运行大型场景或远端 CI，未执行远端操作。
文件格式、所有权、多态声明及校验时机保持不变；没有新增注册表或对象管理服务。
