<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-229：Granit 0.19.0 升级与兼容审计

## 结果

Gneiss 的 FETCH 默认值已从 Granit 0.17.0 升级到 0.19.0 正式标签对应提交
`a185b94f0e15d38ad3903c8fbbc6ba8f557dd5d0`。PACKAGE、安装后依赖和父工程复用路径的最低版本同步
提高到 0.19；既有自定义提交覆盖及关闭默认升级的行为保持不变。

0.19.0 保持原有 Texture 创建与写入语义，并新增以下可供后续里程碑接入的公共契约：

- Texture Asset Manifest 的确定性编码与严格检查；
- 基于设备格式能力和用途的变体选择；
- 校验负载摘要后向空 Upload Batch 原子加入指定 Mip 范围。

Manifest 不包含像素负载，也不承担 Gneiss 的文件读取、缓存、RID 或资源切换。此次升级只建立依赖
基线，不提前改变 0.34.0 的 RGBA8 KTX2 运行路径。

## 本地验证

- Windows Clang Shared Debug 严格警告全量构建通过。
- 完整测试 136/136 通过，包含 Granit 平台 smoke、版本缓存迁移和安装后 Consumer。
- FetchContent 检出的提交与 `v0.19.0` 精确匹配。
- `git diff --check` 通过。
