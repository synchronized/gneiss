<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# GUID 编译期字面量验收

日期：2026-11-05。环境：Windows、windows-clang-debug。

## 完成范围

- 增加 consteval guid::literal，非法 UUID 文本在编译期拒绝。
- guid::parse 改为 constexpr/noexcept；literal 与运行期解析共用同一实现。
- 迁移 src 中 36 处固定字节数组初始化，逐项与提交前的 16 字节值比较，全部一致。
- 保留动态 from_bytes 入口和既有字节布局，未生成或改变类型 GUID。
- 更新 Core 与反射参考。具体规则见 [Core 参考](../reference/core.md)。

## 验证

- windows-clang-debug 构建通过。
- 17 项身份、反射、序列化、Scene/Prefab、IPC 与边界回归全部通过。
- static_assert 覆盖标准字节顺序、大小写、nil、短数组零填充对应值及非法格式解析。
- 独立 C++20 编译探针验证合法字面量通过；错误长度、连字符、十六进制和运行期参数
  均因不满足常量表达式要求而被拒绝。
- 修改源码 clang-format 检查及 git diff --check 通过。

未运行大型场景或远端 CI，未执行远端操作。现有文件格式和类型身份不变。
