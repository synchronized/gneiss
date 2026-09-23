<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-231：BC7 私有工具依赖与确定性编码门禁

## 结论

离线工具采用 `bc7enc_rdo` 的标准 C++ `bc7enc`/`bc7decomp` 核心，锁定提交
`b9438627eef73a1157e84201b6fa6eb2ffd6d9f0`，并选择 MIT 许可。候选源码没有第三方运行依赖；相比
完整 Compressonator SDK 和 ISPCTextureCompressor，不需要引入大型工具套件或额外 ISPC 编译器。

依赖仅链接 `gneiss_asset_import_sdk` 及其测试，不进入 Engine、Runtime、安装 SDK 或公共 ABI。
Gneiss 包装层使用固定参数和一次性线程安全初始化；sRGB 内容使用感知权重，Linear 内容使用等权重，
编码质量级别固定为 1。非 4 倍数尺寸通过复制最外侧像素补足最后一个 4×4 块。

## 验证

- 5×3 RGBA8 输入生成 2×1 个 BC7 块，紧密负载为 32 字节。
- 相同输入和参数连续编码得到逐字节相同结果。
- 使用上游解码器验证所有块有效，平均平方误差与最大 Alpha 误差处于既定门限。
- 零尺寸和不匹配字节数明确失败并返回诊断。
- Windows Clang 严格警告构建及 `gneiss.tooling.bc7_encoder` 测试通过。

后续平台构建会把编码器锁定提交和固定参数版本纳入缓存键；本记录不代表 Runtime 已开始消费 BC7。
