<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 第三方软件声明

Gneiss Runtime 的源码或二进制包含下列第三方软件。安装树在 `share/doc/gneiss/licenses` 中保留
对应许可证原文；版本由仓库锁定提交决定。

| 软件 | 版本 | 用途 | 许可证 |
| --- | --- | --- | --- |
| EnTT | 3.15.0 | ECS 内部实现 | MIT |
| yyjson | 0.12.0 | JSON 解析与写出 | MIT |
| libspng | 0.7.4 | PNG 解码 | BSD-2-Clause |
| miniz | 3.1.2 | libspng 的 Deflate 实现 | MIT |
| libuv | 1.52.1 | 原生线程构建的资产文件 I/O；Editor/Runtime 平台 I/O | MIT |

Granit 是 Gneiss 的外部运行时依赖，由父工程、已安装 package 或锁定源码构建提供；Gneiss 安装树
不复制 Granit 二进制。离线资产工具和 Editor 的可选构建依赖不会随 Runtime SDK 安装；若单独分发
这些工具，应同时携带其构建产物所要求的第三方许可证。

可选离线工具使用锁定提交的 `bc7enc_rdo` BC7 编码与解码核心文件，按 MIT 许可证使用。该依赖只
链接资产工具，不进入 Runtime、Gneiss 安装 SDK 或公共 ABI；单独分发资产工具时必须携带其许可证。

libuv 在核心原生线程构建中私有内嵌，不向普通公共头或安装 package 暴露其类型和目标。
无线程/Web 核心构建保留标准库文件回退；无需自行维护平台 I/O 是采用 libuv 的主要原因，
原生 API 后端仅作为明确能力缺口的后续备选。本版逐步迁移，异步区间读取仍有既有平台实现。

资产管理器内部实现移植自 Granit `examples/common/assets` 的请求共享、类型 Loader 与只读结果模型，
来源提交 `c9803195cc81d2d1cef55e47f5bcff1c42884ccd`，按 MIT 许可保留原版权。
Gneiss 改用现有 VFS/任务执行器，增加有界分步读取、来源复验、同步发布顺序及关闭语义；
这不是未修改的 Granit 示例 target，也不是 Granit 公共 SDK。原许可证安装为
`share/doc/gneiss/licenses/granit-example-assets-LICENSE`。
核心不再链接、获取或安装 CAORS；替换不改变公共 C ABI。
