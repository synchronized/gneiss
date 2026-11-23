<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 共享序列化遍历验收

日期：2026-11-05。环境：Windows、windows-clang-debug。

## 完成范围

- 增加 reader/writer 与通用 serialize/deserialize 入口，支持默认和独立注册表。
- 字段、枚举、容器、可空值、variant、引用及多态、构造和校验移入共享遍历。
- JSON 后端通过借用节点/游标接入；保留专用节点及字节 codec 和已有文件格式。
- 字段遍历与未知字段策略提升为通用配置，通过 serialization() 注册。
- 不新增持有型通用 DOM；JSON 节点遍历不重新编码文本，不构造整容器节点列表。
- 增加无 JSON 依赖的内存测试后端，以及共享遍历禁止依赖 JSON 的边界检查。

接口、生命周期和当前限制见 [序列化参考](../reference/serialization.md)；
取舍见 [ADR-105](../decisions/ADR-105-serialization-format-boundary.md)。

## 验证

- Windows Clang Debug 构建通过。
- 34 项相关测试通过：通用后端、JSON、Meta、Scene/Prefab、资产、IPC、组件与编辑器。
- 另外 3 项类型注册、内部头可见性及架构负向测试通过，共 37 项。
- 通用后端测试覆盖独立注册表、标量、sequence、associative、optional、variant、
  多态引用独立恢复、空引用、循环拒绝、重复/未知/必填字段、错误路径及写入失败。
- 原有深层嵌套、节点 codec 和引用编辑回归通过。
- clang-format 与 git diff --check 通过。

未运行大型场景或远端 CI。本轮未新增通用二进制格式，也未实现流式 writer；
这些不属于本次共享遍历抽取范围。
