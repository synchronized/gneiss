<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-15 版本化场景 Schema 实施记录

- 日期：2026-08-26
- 环境：Windows、Clang、C++20、yyjson 0.12.0
- 结果：完成 Schema v1 纯解析、VFS 读取和完整校验

## 实施结果

- 定义不含运行时 ID、RID 和第三方类型的内部场景中间描述。
- 通过 VFS 同步读取场景字节，严格解析 JSON 后再生成中间描述。
- 校验格式与版本、未知字段、规范 UUID、父引用、循环、Transform 数值、Camera 范围、主相机数量
  和 Mesh/Material URI。
- 失败时保持输出场景为空，并返回稳定结果码、字段路径及语法字节位置；解析不修改 World。

长期格式决策见 [ADR-006](../decisions/ADR-006-scene-schema-v1.md)，当前字段定义见
[当前场景文件格式与迁移规则](../reference/scene-format.md)。
