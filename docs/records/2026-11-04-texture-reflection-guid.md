<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 纹理反射与覆盖 GUID 收敛验收

日期：2026-11-04。范围见 [DEV-052](../plans/DEV-052-architecture-cleanup.md)。

## 已完成

- asset_build 的 read_texture_settings 调用 Engine 纹理 codec，移除第二套字段读取和枚举校验。
- texture_description 注册 source、color_space、usage，复用 reflected_asset_codec 的格式包装。
  必填/未知字段、枚举合法性与 normal 必须配 linear 的约束保留，文件仍为 v2。
- 新增属性标记 enum_name，仅改变枚举属性的 JSON 表达；原生枚举身份和整数 value 不变。
  未标记枚举继续使用数值编码。编码拒绝未知值，解码拒绝数字、null 及未知名称。
- Prefab 覆盖目标 type_guid 使用现有 guid::parse/string，移除 32 字符十六进制专用实现。
  当前样例、夹具、编辑器测试和格式参考同步更新，不保留旧格式兼容。
- CPU 准备测试因反射描述依赖而改为链接 Engine，并初始化 Meta；仍不创建 Application 或 GPU。

## 验证结果

Windows Clang Debug 编译及 12 项测试通过：

- tooling.asset_build、tooling.asset_writer、tooling.asset_import_sdk。
- assets.material、scene_description、prefab_description、render_asset_preparation。
- reflection-json、meta-default-registry、meta-default-registry-failure、meta-default-registry-exception。
- editor.prefab-authoring。

Windows Clang Static Debug 的 tooling.asset_build、tooling.asset_writer、reflection-json
编译及三项测试通过。反射测试覆盖有符号枚举名称往返、未标记属性的数字格式、未知值拒绝及
原有 64 层嵌套边界。枚举处理独立为辅助函数，避免增大每层递归的调试栈占用。

未运行大场景、远端 CI、MSVC/Linux/浏览器矩阵。网格 JSON 反射化及导入索引反射化未在本批实施。
