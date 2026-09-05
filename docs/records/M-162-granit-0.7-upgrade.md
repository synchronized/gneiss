<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-162：Granit 0.7.0 升级与兼容审计

## 结果

Gneiss 的 Granit Fetch 默认值已从 0.5.0 发布基线升级到 0.7.0 发布提交
`fa42c5f479ff98642b42f4cf31c77bc1932715f4`。PACKAGE 查找和 Gneiss 导出 package 的传递依赖均
最低要求 Granit 0.7，构建指南同步声明 0.7.0+。

默认值追踪保留原有语义：仍使用项目旧 0.5 默认值的构建缓存会升级，用户显式固定的提交不会被
覆盖。升级审计确认现有 Window、Input、RenderPipeline、Canvas 和动态 Uniform 路径无需修改即可
构建；0.7.0 新增的 Upload Batch 可供后续上传命令使用，但其提交仍为同步完成语义。

## 验证

- Windows Clang Shared Debug：完整构建及 129/129 测试通过。
- Windows Clang Static Debug：完整构建及 127/127 测试通过。
- 安装 Runtime 与 Stable Runtime Consumer 均使用 Granit 0.7 package 成功配置、构建和运行。
- CMake 默认版本测试覆盖全新默认值、旧 0.5 默认缓存升级、关闭升级以及显式提交覆盖。

## 已知边界

- Linux、GCC、Granit Runtime 无头测试和 Sanitizer 留到 M-168 统一执行。
- Granit Model Viewer 的渲染线程执行器属于示例私有实现，Gneiss 只借鉴其所有权与队列语义。
