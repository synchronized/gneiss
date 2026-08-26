<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-14 资产 URI 与资源生命周期实施记录

- 日期：2026-08-26
- 环境：Windows、Clang、C11/C++20
- 结果：完成首版 URI、目录 Provider 与同步缓存基础

## 实施结果

- 新增公共 C URI 校验和轻量 C++ 包装。
- Application 描述结构末尾增加可选资产根字段，旧版结构大小保持兼容。
- 目录 Provider 使用 canonical 路径限制根目录逃逸，只读普通文件。
- 内部缓存覆盖同 URI 复用、类型冲突、租约回收和失败重试。
- 新增 `not found` 与 `I/O error` 稳定结果码。

长期 URI 契约见 [ADR-005](../decisions/ADR-005-asset-uri.md)，当前接口行为见
[资产参考](../reference/assets.md)。
