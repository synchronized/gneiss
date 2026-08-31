<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-76：Runtime 示例与安装树阶段验收记录

## 结论

Windows VS2022 Shared/Static Debug 的构建树与隔离安装树 Runtime 闭环已经通过。Lantern Gallery 可由
`gneiss_runtime` 直接从构建树工程根启动；Editor Demo 会作为完整工程安装，并可由安装后的 Runtime
在脱离源码资产路径的情况下进入首帧并正常退出。

M-76 尚未完成：Granit Package Provider、Linux 和代表性 Release 矩阵仍需验证。

## 环境

| 项目 | 值 |
| --- | --- |
| 操作系统 | Windows 10 x64 |
| 生成器 | Visual Studio 2022 |
| 配置 | Debug、Shared/Static |
| Granit | Fetch，`a126b5f719ef48215e02a190bb1b4c5b3e5708e8` |
| 测试日期 | 2026-08-31 |

## 验证结果

| 验收项 | 结果 |
| --- | --- |
| 全量构建 | 通过 |
| 构建树 Editor Demo Runtime Smoke | 通过 |
| 构建树 Lantern Gallery Runtime Smoke | 通过 |
| Editor 启动 Lantern Gallery | 通过 |
| 隔离安装树 Editor Demo Runtime Smoke | 通过 |
| 安装后稳定 Runtime Consumer | 通过 |
| Shared 完整 CTest | 78/78 通过 |
| Static 完整 CTest | 78/78 通过 |

全量测试首次发现独立稳定 Runtime Consumer 在 MSVC 默认代码页下无法编译中文字符串。Consumer
目标补充 `/utf-8` 后，单项和全量复测均通过。

## 待验证矩阵

- Granit Package Provider。
- Linux Clang/GCC Shared/Static 及 POSIX 子进程控制。
- 发布前代表性 Release 配置。

Windows Static Debug 首次配置因依赖下载耗时被中止；缓存完成后重试配置、全量构建及 78 项测试均
通过，证明该现象属于首次网络获取耗时，不是静态链接缺陷。
