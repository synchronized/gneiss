<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-233：Texture Manifest 检查与设备变体选择

## 结论

Runtime Loader 在创建 RID 前严格解析 `.gneiss-texture` 外层容器，并调用 Granit 检查 Texture Asset
Manifest。当前运行纹理只接受二维单层资源、BC7/RGBA8 UNORM 或 SRGB 变体，所有变体必须与 Texture
描述的颜色空间一致且负载范围有效，并且必须包含 RGBA8 回退。

经过检查的 Manifest 与 Payload 由 Texture RID 持有，不在逻辑线程提前展开为 RGBA8。Granit Render
Service 在渲染线程拿到真实 Renderer 后调用设备选择接口：支持 BC7 时选择首个 BC7 变体，否则继续
选择 RGBA8 回退；设备不支持任何候选时保留 Granit 的明确失败结果。作者 PNG 和旧 KTX2 路径仍作为
编辑器与旧资产兼容入口。

## 验证

- Loader 测试覆盖运行纹理外层解析、Manifest 检查、元数据保留和 Payload 所有权。
- 完整 Windows Clang 严格警告构建通过。
- 140 项本地测试中 139 项首次通过；Project Workspace 因仍断言旧 `.ktx2` 路径失败，更新为
  `.gneiss-texture` 后单独复测通过。
- Lantern Gallery 的 Runtime 与 Editor 工作流测试通过。

当前渲染线程仍使用逐 Mip 直接写入。摘要验证、原子 Batch 提交和失败时保留旧 GPU 资源由 M-234
接替。
