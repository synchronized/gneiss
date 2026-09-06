<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-181：Granit 0.12.0 升级与兼容审计

## 结果

Gneiss 的 Granit Fetch 默认值已升级到 0.12.0 正式发布提交
`6f1bc9339bc0ab21bc5a11517b9f10dc90b670ca`。PACKAGE 查找、导出 package 的传递依赖和构建指南
均以 Granit 0.12 为最低版本。

默认缓存迁移只更新仍使用 Gneiss 0.10 默认提交的构建目录，用户显式固定的 Granit 提交保持不变。

## 兼容审计

- Granit 0.12 没有修改现有 Shader、Material 和 Environment 持久格式。
- 新增公共 Shader Asset 元数据检查、标准 PBR Schema/Material 和统一 RenderPipeline 资产目录，
  可以替换 Gneiss 0.27 的临时重复实现。
- Gneiss 作者资产、RID、Scene 与 IPC 边界不受影响，无需公共 API 或数据迁移。
- Granit 0.11 的 WebGPU 对等能力只纳入后端资产契约验证，不改变 Gneiss 当前平台承诺。

## 本地验证

- Windows Clang Shared Debug 完整构建成功。
- `gneiss.granit_version_cache` 与 `gneiss.granit_platform_smoke` 通过。
- 其余完整矩阵在 M-186 统一执行。
