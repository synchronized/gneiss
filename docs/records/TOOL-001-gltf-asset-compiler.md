<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# TOOL-001 glTF 资产编译器验收记录

## 结论

2026-08-27 完成 glTF 2.0/GLB 静态资产编译最小闭环。`gneiss_assetc` 可将受支持的节点层级、
Triangle List Mesh、法线、UV、基础颜色与 PNG 转换为 Gneiss Runtime 资产；Runtime 和安装包不依赖
fastgltf。真实 Khronos Lantern GLB 已通过导入、场景组装和 Granit 渲染 Smoke Test。

## 已验收能力

- `.gltf`、`.glb`、Data URI、外部 Buffer 与嵌入 BufferView PNG。
- 节点 TRS、多 Primitive 拆分、缺省材质及确定性资源命名。
- Mesh Binary v1、事务目录替换、重复导入清理和失败恢复。
- Accessor、有限值、拓扑、索引、必需属性和受支持范围校验。
- 外部资源拒绝绝对路径、网络 URI、父目录或编码逃逸；可创建符号链接的平台同时校验真实目标。
- `inspect`、`import`、Mesh Binary `validate` 与按需 JSON `dump`。

## 明确不支持

- 动画、骨骼、Morph Target、Draco、Meshopt 和任意必需扩展。
- 完整 PBR、非 PNG 图片、FBX、OBJ、USD 或 Runtime glTF 解析。
- 增量资产数据库、编辑器进程协议和稳定公共导入 SDK。

## 本地验证

- Windows Clang Debug 默认工具构建：36 项测试。
- Windows Clang Debug + Granit：38 项测试，包含 Temple、Lantern 与平台 Smoke Test。
- `GNEISS_BUILD_TOOLS=OFF`：31 项测试；构建图与安装路径不包含 fastgltf、simdjson 或
  `gneiss_assetc`。
- C11/C++20 公共头、共享库和安装后 Consumer。
- 相关源码格式与静态检查、`git diff --check`。

Mesh Binary 的体积和加载结果见 [TOOL-002 记录](TOOL-002-mesh-binary-v1.md)，索引绘制结果见
[TOOL-003 记录](TOOL-003-indexed-rendering.md)。
