<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-17 场景实例化与版本验收记录

- 日期：2026-08-26
- 环境：Windows/Linux、Clang/GCC/MSVC、共享/静态、Granit Fetch
- 结果：完成原子实例化、公共接口、示例迁移、安装 Consumer 与远端矩阵

## 实施结果

- 新增公共场景实例加载、卸载和 UUID 节点查询接口及 C++ RAII 包装。
- 加载严格分为描述验证、资产暂存和 World 提交；正常卸载与失败使用同一逆序清理路径。
- 成功加载三角形场景创建 2 个实体，卸载后恢复为 0；缺失资产失败时 World 始终为 0。
- 三角形示例只指定资产根和场景 URI；Mesh、Material、Camera、对象和组件均来自资产文件。
- Windows Clang 共享/静态核心测试通过；Granit Fetch 配置构建成功，包含窗口 smoke 的测试通过。
- 安装内容包含公共头文件、库、CMake package 与资产；独立 C11/C++20 Consumer 可通过
  `find_package(gneiss)` 构建，并从 `GNEISS_ASSET_DIR` 加载场景。
- Pull Request #3 的 Linux Clang/GCC、Windows MSVC、共享/静态与 Granit 运行时矩阵通过。

Texture、异步加载与热重载不属于本次 P0 验收，分别保留在 M-18 和后续需求验证中。

> 后续版本已将这些测试 fixture 迁出正式安装包；当前安装行为以
> [构建指南](../guides/building.md)为准。
