<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-108：Runtime 检查示例与跨平台验收记录

## 结论

截至 2026-09-02，Lantern Gallery 已能产生并验证真实 Runtime 属性变化；Windows Clang Shared
与 Static 的 Debug、Release 应用层矩阵均已通过。Linux 及 Sanitizer 尚未完成，因此 M-108 保持
进行中。

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

## 待完成

- 推送特性分支后手动运行 Linux Shared/Static 与 Sanitizer Actions。
- 汇总最终矩阵后将 M-108 和 0.16.0 标记为完成。
