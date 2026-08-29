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

Granit 是 Gneiss 的外部运行时依赖，由父工程、已安装 package 或锁定源码构建提供；Gneiss 安装树
不复制 Granit 二进制。离线资产工具和 Editor 的可选构建依赖不会随 Runtime SDK 安装；若单独分发
这些工具，应同时携带其构建产物所要求的第三方许可证。
