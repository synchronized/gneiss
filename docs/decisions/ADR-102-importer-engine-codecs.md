<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-102：导入工具复用 Engine 描述与编码

- 状态：已接受。
- 日期：2026-11-04。

## 决策

按用户确认，离线导入 SDK 链接 Engine，直接构造资源描述并调用共享 codec。
材质、场景使用现有反射编码；纹理描述使用共享显式 codec，保留字符串枚举与版本 2 格式。
纹理 usage 成为描述字段，读取后不再丢弃。此决策不要求把二进制载荷迁移到反射。

SDK 不重复编译 Engine 的网格/纹理编码和 SHA-256 实现；这些内部原生入口由同一 Engine 提供。
导入时只需初始化 Meta，不创建 Application、World、窗口或 GPU 服务。导入的第三方解析器
继续留在 Tooling，Engine 不反向依赖 Tooling。

## 影响与替代方案

导入工具部署须携带 Engine 及链接依赖；静态构建沿用 Engine 静态依赖闭包。
共享进程只使用一份类型注册表。宿主如需自定义类型，必须在首次导入前通过已有初始化回调注册，
导入器不会创建第二份注册表或重置已冻结注册表。

暂不抽取独立 Core/Meta/Codec 库：该方案需要重新划分导出及注册表归属，目前没有独立轻量 SDK
部署需求支撑这项成本。后续确有需求再评估。

验收要求：无 Application 的 SDK 导入测试通过；输出可由 Engine 解码；字符串转义、稳定 UUID、
多 Primitive 子节点、纹理用途与错误保留旧输出覆盖；受影响共享及静态目标可链接。
