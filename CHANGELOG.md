<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 变更记录

本文件记录面向使用者的重要变化。版本尚未发布的内容统一保留在“未发布”章节。

## 未发布

- 增加版本化 Camera、活动 Camera 管理、右手视图与 Vulkan 透视投影约定。
- Granit 路径增加 D32 深度缓冲、真实裁剪空间投影和与绘制顺序无关的 3D 遮挡。
- 增加 Mesh v3 逐顶点法线、逆转置法线变换、方向光和环境光，保持旧 Mesh 无光照兼容。
- 将原创片麻岩神殿升级为立体地面、石柱、横梁与祭坛场景，并支持 `A`/`D` 轨道观察。
- 将神殿配套资产收拢到示例目录，并支持从构建树或安装前缀独立定位。
- 将三角形 fixture 迁入测试数据目录，正式引擎安装包不再携带通用资产目录。

## 0.4.0 - 2026-08-27

- 增加 Texture C11/C++20 契约、RGBA8 像素格式、线性/sRGB 颜色空间和 RID 生命周期。
- 使用内部 libspng/miniz 从 VFS 解码 PNG，并通过版本化 Texture 描述创建缓存租约。
- 增加 Mesh/Material v2 的 UV、base-color Texture URI 和依赖租约，保持 v1 资产兼容。
- Granit 后端增加 Texture/View 镜像、Sampler、材质 Bind Group 批次和默认白纹理。
- 增加三张原创纹理及可交互的 2.5D 片麻岩神殿示例，支持显式 Z 绘制层级。
- 本地 Windows Clang 静态核心、共享 Granit、安装 Consumer 和真实纹理场景验收通过。

## 0.3.0 - 2026-08-27

- 增加后端无关的 C11 输入 ABI、C++20 包装、帧状态快照和固定容量原始事件队列。
- 接入 Granit Input 后端、焦点清理与无输入座席降级，保持无头环境的窗口和渲染能力。
- 增加版本化动作映射 JSON v1、VFS 事务加载、代次句柄和 Application 隔离。
- 增加 Application 级同步诊断回调，以及稳定的严重度、类别、结果码、模块和消息字段。
- 将资产驱动三角形示例改为通过动作映射响应 `A`、`D` 和 `Esc`。
- 完成 Windows/Linux、共享/静态、Granit 运行时及安装后 C11/C++20 Consumer 验收。

## 0.2.0 - 2026-08-26

- 锁定 yyjson `0.12.0` 作为内部 JSON 解析依赖，并验证严格 UTF-8、精确整数和错误位置行为。
- 增加严格的 `asset://` URI、可挂载 VFS、本地文件系统目录逃逸防护和内部资源缓存基础。
- 增加版本化场景 Schema v1、VFS 读取、纯中间描述和 UUID、层级、组件字段完整校验。
- 增加 Mesh/Material JSON v1、VFS Loader、RID 缓存租约和失败重试闭环。
- 增加原子场景实例加载、卸载、UUID 节点查询，并将三角形示例迁移为完全由资产驱动。
- 增加可重定位的 CMake package、安装资产目录及共享/静态 C11、C++20 Consumer 验收。

## 0.1.0 - 2026-08-26

- 建立 C11 公共 ABI 和轻量 C++20 包装。
- 增加 Application 生命周期、时间、暂停、退出与 Granit Window 平台适配。
- 增加 World、Entity、确定性 System 调度和基于 EnTT 的内部 ECS 存储。
- 增加 Scene Tree、实体映射与层级 Transform。
- 增加 Mesh、Material RID、Camera、Mesh Renderer 和 World 渲染快照。
- 增加基于 Granit 的 Triangle List 渲染闭环、固定帧数 smoke test 和旋转三角形示例。
- 增加 Granit 父工程、已安装 package 与锁定源码下载三种依赖解析路径。
