<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-17 场景实例化与本地验收记录

- 日期：2026-08-26
- 环境：Windows、Clang、共享/静态、Granit Fetch
- 结果：完成原子实例化、公共接口、示例迁移与本地矩阵

## 实施结果

- 新增公共场景实例加载、卸载和 UUID 节点查询接口及 C++ RAII 包装。
- 加载严格分为描述验证、资产暂存和 World 提交；正常卸载与失败使用同一逆序清理路径。
- 成功加载三角形场景创建 2 个实体，卸载后恢复为 0；缺失资产失败时 World 始终为 0。
- 三角形示例只指定资产根和场景 URI；Mesh、Material、Camera、对象和组件均来自资产文件。
- Windows Clang 共享/静态核心测试通过；Granit Fetch 配置构建成功，包含窗口 smoke 的测试通过。

Linux、MSVC、安装 Consumer 与远端 Actions 仍需在特性分支达到可评审状态并获授权推送后完成。
