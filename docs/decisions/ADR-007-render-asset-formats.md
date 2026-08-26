<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-007：首版 Mesh 与 Material 运行时资产格式

- 状态：已接受
- 日期：2026-08-26

## 背景

0.2.0 需要让场景通过逻辑 URI 加载 Mesh 与 Material，而不是在示例代码中构造顶点和颜色。当前
渲染能力只有位置顶点、Triangle List 和固定线性颜色，不应提前引入完整 glTF、材质图或导入管线。

## 决策

- Mesh 使用严格 JSON，格式标识 `gneiss.mesh`、版本 `1`、固定 `triangle_list` topology 和位置
  vertices；顶点数至少为 3 且是 3 的倍数，所有分量必须是有限 float。
- Material 使用严格 JSON，格式标识 `gneiss.material`、版本 `1` 和线性 RGBA color；分量位于 0..1。
- 建议扩展名分别为 `.mesh.json` 与 `.material.json`。同版本拒绝未知字段，未来版本返回 unsupported。
- Loader 通过 VFS 读取并解析中间值，再调用现有 Render Resource Service 创建 RID。
- 缓存以规范 URI 和资源类型管理拥有型租约。租约借出 RID；最后一个租约释放并清理缓存后销毁 RID。
- 文件格式、yyjson 和 VFS 后端类型保持内部，不进入公共 ABI。

## 影响

首版资源路径完整可测，且没有把源内容格式或第三方导入器变成运行时 ABI。当前 JSON Mesh 适合最小
样例而非大规模内容；出现真实模型需求后应增加离线转换工具和紧凑运行时格式，而不是无限扩展该格式。

## 替代方案

- 运行时直接加载 OBJ/glTF：快速获得复杂内容，但会把导入、坐标转换和第三方依赖带入运行时核心。
- 立即设计二进制包格式：性能潜力更高，但当前字段过少，缺乏版本和对齐需求依据。
- 继续由代码创建资源：无法验证场景持久化、VFS、缓存和失败回滚路径。
