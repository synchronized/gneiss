<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-108：Runtime 检查示例与跨平台验收记录

## 结论

M-108 已完成。Lantern Gallery 已能产生并验证真实 Runtime 属性变化；Windows Clang
Shared/Static Debug/Release 应用层矩阵，以及 Linux GCC/Clang、Shared/Static、Granit Runtime
和 Sanitizer 均已通过。

## 示例闭环

- 游戏模块在无输入时持续旋转场景根实体，输入仍可叠加旋转速度。
- Runtime 从 ECS World 读取实体的实时 Transform，而不是重复发送作者场景中的初始值。
- 自动测试验证首次镜像、旋转变化、暂停稳定、恢复后继续变化、正常停止及新会话隔离。
- 暂停确认后排空检查队列中的在途快照，避免把暂停前已发送但尚未应用的数据误判为暂停失效。

## Windows 验证结果

| 配置 | 范围 | 结果 |
| --- | --- | --- |
| Clang Shared Debug | 全量构建与测试 | 修正排空窗口后专项连续 10 次通过，全量 103/103 通过 |
| Clang Static Debug | 启用 Tools、Editor、Runtime 的全量构建与测试 | 101/101 通过 |
| Clang Shared Release | 启用 Tools、Editor、Runtime 的全量构建与测试 | 103/103 通过 |
| Clang Static Release | 启用 Tools、Editor、Runtime 的全量构建与测试 | 101/101 通过 |

## Linux 远端矩阵

[Linux Actions 运行 33614582514](https://github.com/synchronized/gneiss/actions/runs/33614582514)
基于候选提交 `ab1af14`，以下 7 项 Job 全部通过：

- GCC Core Shared 与 Static。
- Clang Core Shared 与 Static。
- Granit Runtime Shared 与 Static，包括无头窗口测试。
- Sanitizer Runtime，包括内存错误与退出泄漏检查。
