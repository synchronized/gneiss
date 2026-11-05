<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 类型序列化与资源文件包装分离验收

日期：2026-11-05。环境：Windows、windows-clang-debug。

## 完成范围

- 资源格式头、版本、文件大小及已移除字段检查集中到 asset_file 文件入口。
- 普通资源描述按反射字段保存，作为嵌套对象或容器元素时不携带文件头。
- value 标签与对象内容编码迁入通用 reader/writer；JSON 后端复用同一实现。
- 删除 json_serialization、json_bytes_serialization 及 Meta 中的 JSON 回调。
- 自定义类型使用 context-first 的通用 serialization 回调，普通类型继续使用字段遍历。
- 资产加载、导入、场景、编辑器保存及相关测试迁移到明确的文件或对象入口。

当前接口见[序列化参考](../reference/serialization.md)，决策见
[ADR-105](../decisions/ADR-105-serialization-format-boundary.md)。

## 验证

- `cmake --build --preset windows-clang-debug -j5` 通过。
- 54 项相关测试与边界检查通过，覆盖 Meta/JSON、内存 reader/writer、Scene/Prefab、
  资产加载、纹理、导入、IPC、编辑器保存及核心/反射/内部头边界。
- 通用 value 测试不依赖 JSON，覆盖标量、空引用、派生对象独立恢复及未知标签。
- 材质描述验证普通对象和容器元素无文件头、文件入口拒绝缺失格式头；
  资源格式版本、文件大小、校验与原有错误回归通过。
- 48 个修改或新增 C++ 文件的 clang-format 检查与 `git diff --check` 通过。

未运行大型场景或远端 CI；未执行远端操作。
未新增通用二进制格式；现有资源文件仍使用原有 JSON 格式。
