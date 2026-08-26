<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 变更记录

本文件记录面向使用者的重要变化。版本尚未发布的内容统一保留在“未发布”章节。

## 未发布

- 锁定 yyjson `0.12.0` 作为内部 JSON 解析依赖，并验证严格 UTF-8、精确整数和错误位置行为。
- 增加严格的 `asset://` URI、可挂载 VFS、本地文件系统目录逃逸防护和内部资源缓存基础。
- 增加版本化场景 Schema v1、VFS 读取、纯中间描述和 UUID、层级、组件字段完整校验。
- 增加 Mesh/Material JSON v1、VFS Loader、RID 缓存租约和失败重试闭环。

## 0.1.0 - 2026-08-26

- 建立 C11 公共 ABI 和轻量 C++20 包装。
- 增加 Application 生命周期、时间、暂停、退出与 Granit Window 平台适配。
- 增加 World、Entity、确定性 System 调度和基于 EnTT 的内部 ECS 存储。
- 增加 Scene Tree、实体映射与层级 Transform。
- 增加 Mesh、Material RID、Camera、Mesh Renderer 和 World 渲染快照。
- 增加基于 Granit 的 Triangle List 渲染闭环、固定帧数 smoke test 和旋转三角形示例。
- 增加 Granit 父工程、已安装 package 与锁定源码下载三种依赖解析路径。
