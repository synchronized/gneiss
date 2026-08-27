<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# TOOL-001：glTF 资产编译器最小闭环

## 状态

- 状态：进行中
- 优先级：P1
- 前置条件：0.5.0 基础 3D 场景闭环完成

## 背景与目标

按照 [ADR-013](../decisions/ADR-013-gltf-import-boundary.md) 建立可复用的内部导入核心和
`gneiss_assetc` 薄命令行前端。首个闭环把一个受控的 glTF 2.0/GLB 静态场景确定性转换为当前
Gneiss 运行时可加载的资产，并用现有渲染示例验证结果。

## 非目标

- FBX、OBJ、USD、Blender 工程文件或通用格式插件系统。
- 骨骼、动画、Morph Target、Draco、Meshopt、完整 PBR 和任意 glTF 扩展。
- 在 Runtime 中解析 glTF，或公开 fastgltf 类型。
- 首版冻结公共 SDK、进程协议或长期资产数据库契约。

## 已确认决策

- 源码位于 `src/tooling/asset_import`，CLI 位于 `tools/assetc`。
- 内部 target 暂命名为 `gneiss_asset_pipeline_internal`，可执行文件命名为 `gneiss_assetc`。
- fastgltf 锁定版本和提交，只链接工具 target，不由 `gneiss::gneiss` 或安装 package 传播。
- 首版支持节点 TRS、三角形静态 Mesh、索引、POSITION、NORMAL、TEXCOORD_0、基础颜色和 PNG。
- 不支持的必需属性或扩展必须明确失败，不静默丢弃；诊断包含源文件和对象位置。
- fastgltf 锁定为 `v0.9.0`（提交 `0d1b67a28c4950ea2deb796702006dcbe31e02b3`，MIT）；
  随附 simdjson `3.12.3`（Apache-2.0）作为静态实现编译，禁止自动拾取系统 package。
- 工具构建关闭时不下载或配置 fastgltf；测试、示例、文档、安装和 C++ Module 均关闭。

## 实施顺序

1. 接入锁定 fastgltf，建立 Import IR、诊断结果和依赖清单。首个 `inspect` 闭环已完成。
2. 读取 `.gltf` 与 `.glb`，执行边界、有限值、拓扑、索引和资源 URI 校验。已完成 BufferView/
   Accessor 边界、静态三角形拓扑及必需属性校验，其余校验继续补齐。
3. 映射坐标、节点层级、静态 Mesh、基础材质和 PNG，写出 Gneiss 自有资产。
4. 增加 `gneiss_assetc import <source> --output <directory>`，CLI 只负责参数与结果呈现。
5. 以最小原创 fixture 验证导入、重复生成、错误输入和运行时加载，并记录性能与产物差异。

## 测试与验收

- 相同输入重复导入生成逐字节一致的 JSON 和稳定资源名称。
- `.gltf`、`.glb`、外部 buffer、嵌入 buffer、索引与非索引三角形覆盖成功路径。
- 缺失法线、越界 accessor、目录逃逸、非有限 Transform 和不支持扩展覆盖失败路径。
- 导入结果由现有 Scene、Mesh、Material 和 Texture Loader 加载，并完成渲染 Smoke Test。
- Runtime、公共头、安装 Consumer 和关闭工具构建的配置不依赖 fastgltf。

## 风险与未决问题

- 当前 Mesh v3 使用展开顶点；是否保留索引需要由导入产物大小和渲染路径共同决定。
- 纹理复制、命名冲突和增量缓存策略先通过 fixture 验证，不在首版建立全局资产数据库。
- 编辑器进程协议和公共 SDK 留待编辑器宿主计划出现真实调用需求后决定。
