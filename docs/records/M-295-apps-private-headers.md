<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-295：Apps 私有头与宿主共用契约

日期：2026-10-06。

## 变更

34 个 Apps 私有 C++ 头改为 `.hpp`，同步 CMake、应用、示例及测试引用。
`apps/common` 两个私有目标继续分别拥有工程/启动契约和 IPC 领域协议；不安装为 Engine SDK，
Runtime 不链接 Editor。所有权依据见 [ADR-054](../decisions/ADR-054-source-layout-and-host-boundaries.md)。

Editor 对宿主协议的禁止包含规则同时覆盖 `.h` 和 `.hpp`，正反例检查保留两种后缀。
公共 C 头、导出、协议内容和可执行文件名不变；历史 Record 与 Version 保留当时路径。

## 验证

- 前序引擎边界迁移共享全量回归 181/181 通过，194.51 秒；包含安装消费、GPU、Editor/Runtime。
- 本次改名后共享完整构建通过；Apps、Editor、Runtime 与边界相关测试 68/68 通过，145.73 秒。
- 单独运行边界检查及包含旧/新后缀的违规反例，通过。
- 后续静态整合及共享完整回归见 [作者属性面板记录](M-295-author-property-panel.md)；最终跨平台发布矩阵待验收，资产面板仍在拆分。
