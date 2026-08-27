<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-015：索引 Mesh 与渲染后端边界

- 状态：已接受
- 日期：2026-08-27

## 背景

Mesh Binary v1 已保存唯一顶点与 UInt32 索引，但 Render Service 只能接收 Triangle List 顶点，
Loader 必须展开索引。这会增加 CPU 常驻数据、逐帧变换、GPU 上传和顶点着色工作量。

## 决策

- 在 `gneiss_mesh_desc` 末尾追加可选 UInt32 索引，旧 v1/v2 描述继续按 `struct_size` 兼容。
- Resource Service 复制并拥有顶点、法线和索引；索引必须组成 Triangle List 且不得越界。
- 无索引 Mesh 继续受支持，后端可在提交时生成顺序索引，不改变旧调用方行为。
- Render Service 与 Mesh Binary 使用后端无关索引；Granit 只负责创建 Index Buffer 和提交
  Indexed Draw，不拥有资产格式或迁移逻辑。
- 首版只支持 UInt32，不在公共 ABI 中暴露 Granit 的索引类型。

## 影响

二进制 Mesh 可从 Loader 到 Granit 保留唯一顶点。公共描述结构增大，但已有字段布局不变；调用方
必须使用 `GNEISS_MESH_DESC_INIT` 或正确设置 `struct_size`。后端当前仍逐帧生成动态顶点与索引缓冲，
静态 GPU Mesh 镜像属于后续独立优化。

## 替代方案

- Loader 继续展开索引：无需修改 ABI，但永久丢失二进制格式的主要结构收益。
- 让 Granit 定义 Mesh 资产：会把 Gneiss 的 Runtime 格式和资源生命周期绑定到单一后端。
- 同时支持 UInt16/UInt32：增加 ABI 与分支复杂度，当前资产统计不足以证明收益。
