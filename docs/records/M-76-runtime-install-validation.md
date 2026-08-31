<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-76：Runtime 示例与安装树阶段验收记录

## 结论

Windows VS2022 Shared/Static Debug、Shared Release 与 Granit Package Provider Debug 的构建树和隔离
安装树 Runtime 闭环已经通过。Lantern Gallery 可由 `gneiss_runtime` 直接从构建树工程根启动；
Editor Demo 会作为完整工程安装，并可由安装后的 Runtime 在脱离源码资产路径的情况下进入首帧
并正常退出。

M-76 尚未完成：Linux 矩阵仍需验证。

## 环境

| 项目 | 值 |
| --- | --- |
| 操作系统 | Windows 10 x64 |
| 生成器 | Visual Studio 2022、Ninja Clang |
| 配置 | Debug Shared/Static、Release Shared、Package Debug Shared |
| Granit | Fetch/Package，`a126b5f719ef48215e02a190bb1b4c5b3e5708e8` |
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
| Shared Debug 完整 CTest | 78/78 通过 |
| Static Debug 完整 CTest | 78/78 通过 |
| Shared Release 完整 CTest | 78/78 通过 |
| Granit Package Debug Shared 完整 CTest | 78/78 通过 |

全量测试首次发现独立稳定 Runtime Consumer 在 MSVC 默认代码页下无法编译中文字符串。Consumer
目标补充 `/utf-8` 后，单项和全量复测均通过。

## 待验证矩阵

- Linux Clang/GCC Shared/Static 及 POSIX 子进程控制。

Windows Static Debug 首次配置因依赖下载耗时被中止；缓存完成后重试配置、全量构建及 78 项测试均
通过，证明该现象属于首次网络获取耗时，不是静态链接缺陷。

首次 Package Provider 构建使用了旧的本地 Granit SDK，其中缺少 `renderer_resource_stats`，因此在
编译期被正确拒绝。随后从锁定提交独立构建并安装 Granit SDK，Gneiss 仅通过 `find_package` 消费该
安装包；全量构建与 78 项测试均通过。
