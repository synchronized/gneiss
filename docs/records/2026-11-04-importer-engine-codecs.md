<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 导入器共享 Engine codec 验收

日期：2026-11-04。决策见 [ADR-102](../decisions/ADR-102-importer-engine-codecs.md)，
范围见 [DEV-052](../plans/DEV-052-architecture-cleanup.md)。

## 实施结果

- SDK 链接 Engine，移除重复编译的 mesh_binary、texture_binary、texture_ktx2、png_decoder、sha256。
- 材质、场景从导入 IR 构造原生描述，调用现有反射 codec；节点名称随描述正确保存。
- 纹理描述补齐 usage，读写共用 Engine 入口，保持 v2 字符串字段和 normal/linear 约束。
- 导入首次写出调用 initialize_meta；无 Application 的 SDK 测试验证可直接导入。
- 保留稳定对象 UUID、多 Primitive 合成子节点、重复导入稳定性及失败保留旧输出。
- PNG 分配失败注入测试保留本地编译实现，使可执行文件中的 operator new 能覆盖解码分配；
  此测试用途与 SDK 重复编译清理不同，没有删掉失败测试。

## 验证

Windows Clang Debug 共享构建及以下 11 项测试通过：

- tooling.asset_writer、tooling.asset_import_sdk、tooling.asset_build。
- mesh_binary、texture_ktx2、texture_binary。
- assets.manager、assets.material、tooling.sha256、png_decoder、scene-load-application。

Windows Clang Static Debug 的 asset_writer、asset_import_sdk 构建及两项测试通过。
新增验证覆盖名称/URI 的 JSON 转义、纹理 usage 往返、拒绝 srgb 法线纹理。

未运行大场景、远端 CI、MSVC/Linux/浏览器矩阵；未宣称完成整个 0.52 版本。
纹理和网格完全反射化不在本批交付范围内，二进制载荷仍使用专用编码。
