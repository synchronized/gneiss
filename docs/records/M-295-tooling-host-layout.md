<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-295：Tooling、assetc 与宿主入口布局

日期：2026-10-06。

## 实施范围

离线导入库的构建定义归入 `src/tooling/`，13 个内部头改为 `.hpp`；
assetc CLI 与自身构建定义迁至 `apps/assetc/`，工具测试构建定义归入 `tests/tooling/`。
保持目标名、可执行文件名、导入算法及依赖不变，删除仅转发子目录的旧 tools 构建入口。

9 个性能测量与 Python 测试脚本迁至 `scripts/performance/`，同步当前指南、示例和 Actions 路径。
历史 Record 与已发布版本归档保留执行时路径。

场景保存、启动条件判断和启动请求值从 Apps 迁入 `gneiss_editor_session` 的
`runtime_launch.hpp/.cpp`；该实现只依赖编辑会话与文件系统，不包含进程和工程协议。
真正的进程启动及协议装配仍由 Apps 管理。

## 验证结果

- Windows Clang Debug 共享配置全量增量构建通过。
- 完整共享 CTest 175/175 通过，184.62 秒，包含工具、启动、安装消费者与边界检查。
- 迁移后的 Sponza 准备脚本测试 4/4、纹理清单脚本测试 2/2 通过。
- 本轮尚未重跑静态库及远端矩阵；Editor 面板拆分、SDK 审计和最终发布验收仍待完成。
