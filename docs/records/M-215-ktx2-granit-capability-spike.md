<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-215：KTX2 与 Granit 纹理能力 Spike 记录

## 结论

KTX2 继续定位为 Gneiss 离线工具链候选，不由 Granit 解析。后续需要生产级编码和 Basis Universal
转码时，优先评估 KTX-Software 4.4.2 的私有工具目标，而不是分别拼装容器与转码器。其项目主体采用
Apache-2.0，但启用可选组件前仍须按上游 REUSE 清单审计逐文件许可证；非开源 `etcdec.cxx` 不进入
Gneiss 构建。

当前版本不引入该大型依赖。新增轻量 KTX2 容器探针，只验证标识、尺寸、层/面、Mip Level Index、
超级压缩方案及代表性 RGBA8 UNORM/SRGB 元数据，为后续处理器提供早期错误诊断，不承担解码或转码。

参考来源：

- [KTX-Software 官方仓库](https://github.com/KhronosGroup/KTX-Software)
- [Basis Universal 官方仓库](https://github.com/BinomialLLC/basis_universal)

## Granit 能力审计

Granit 当前已经具备：

- 1D/2D/3D/Cube、Mip 数量和数组层描述；
- 指定 Mip、数组层与区域的 Texture 写入；
- `bytes_per_row`、`rows_per_image` 与格式块 Footprint 结构。

当前缺口是格式枚举仅包含非压缩颜色与深度格式，Texture 写入契约也明确限制为非压缩颜色。因此
RGBA8 KTX2 的各级 Mip 可以表达，但 BC、ETC2 与 ASTC 不能形成跨后端正式上传路径。

## 建议用户提交的 Granit PR

目标：增加通用块压缩 Texture 契约，不引入 KTX2 或任何资产容器依赖。

最小范围：

1. 增加 BC1 RGBA、BC3 RGBA、BC7 RGBA、ETC2 RGBA8、ASTC 4×4 的 UNORM/SRGB 格式。
2. 让格式 Footprint 返回正确块宽、块高和每块字节数。
3. 增加 Renderer 格式能力查询，至少区分采样、传输目标和当前设备是否支持。
4. Texture 与 Upload Batch 写入接受块压缩数据，并校验非边缘区域的块对齐、行跨度和数据大小。
5. Vulkan、D3D12、WebGPU 映射及 Mock 测试覆盖成功、设备不支持、错位区域和容量不足。

Gneiss 接入方式：根据目标平台和能力选择构建产物，解析 KTX2 Level Index 后逐 Mip/层提交；任何
KTX2 类型、枚举和头文件都不跨越 Granit 公共接口。

## 验证

- 代表性 2×2 RGBA8 sRGB KTX2 容器包含两级 Mip，探针正确识别尺寸、Alpha、传递函数与无超级压缩。
- 损坏标识和越界 Mip 范围被拒绝。
- Windows MSVC Debug 目标构建及 `gneiss.tooling.ktx2_probe` 测试通过。

