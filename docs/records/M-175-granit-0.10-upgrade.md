<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-175：Granit 0.10.0 升级与兼容审计

## 结果

Gneiss 的 Granit Fetch 默认值已从 0.7.0 升级到 0.10.0 正式发布提交
`03fc7bbbc2dd9d321444087497ef3eaf9d676ee3`。PACKAGE 查找和 Gneiss 导出 package 的传递依赖均
最低要求 Granit 0.10，构建指南同步声明 0.10.0+。

默认值追踪保留原有语义：仍使用项目旧 0.7 默认值的构建缓存会升级，用户显式固定的 Granit 提交
不会被覆盖。现有 Window、Input、RenderPipeline、Canvas 和动态 Uniform 接入无需源代码改动。

## 兼容性审计

- Granit 0.8 更新了 Shader Asset Manifest，并将 Material 二进制格式升级至 `.grmat` v4。
- Granit 0.9 增加公共 PBR 资产能力。
- Granit 0.10 增加公共 Environment Map，并将环境资产升级至 `GRENV` v3；Model Viewer 同时增加
  Frame Packet 构造前背压。
- Gneiss 当前渲染路径不直接持久化或读取上述 Granit 专用资产格式，因此无需迁移现有项目资产。
- Gneiss 0.26 已有自己的构造前背压，不再需要为该能力向 Granit 提交 Pull Request；Frame Packet
  回收与稳定资源绑定仍属于 Gneiss 上层执行策略，是否下沉应等待跨项目用例和性能证据。

## 验证

- Windows Clang Shared Debug：配置、完整构建及 131/131 测试通过。
- Windows Clang Static Debug：配置、完整构建及 129/129 测试通过。
- 两套配置均从锁定提交重新获取 Granit；Fetch 默认缓存升级测试通过。

## 已知边界

- 尚未执行 Linux、GCC、Sanitizer 和远端 Actions 验证。
- 本记录只确认依赖升级兼容性，不提前启用 Granit 0.8～0.10 的新增资产能力。
