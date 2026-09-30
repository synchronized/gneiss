<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# UPSTREAM-044：独立纹理变体负载上传

## 状态与审计范围

2026-09-30 用户已完成上游提交：[PR #125](https://github.com/synchronized/granit/pull/125)
已合并，[Granit 0.44.0](https://github.com/synchronized/granit/releases/tag/v0.44.0) 已发布。
Gneiss 更新固定依赖到 `369adc7ebc9e202056c5d583bfd8d65243de6a4d`，已接入
`write_texture_asset_variant_mips` 并通过本地非零偏移像素回归。下面保留 0.43 接口审计与原始需求；
接入验证状态见本页末尾。

2026-09-30 已向用户提出 U44-01，尚未提交上游 Issue/PR，也未修改 Granit。
审计对象为 Gneiss 当前锁定的 Granit 0.43.0，提交
`555b1144b02be409c74b7cc971eff2e0ecf74e25`；不表示已检查未锁定的上游更新。
这是公共便利接口扩展需求，不把当前接口按完整 Payload 基址工作认定为缺陷。

关联范围见 [VER-044](VER-044-0.44.0-texture-residency.md)。用户负责向 Granit 提交，
Gneiss 继续承担 VFS、Manifest 来源、资源预算、任务调度与 RID 生命周期。

## U44-01：支持以变体起点为基址的完整变体上传

- 优先级：P0 接入需求；状态：已提出。
- 影响：现有便利接口不能普遍接收独立读取的非零偏移变体，阻塞直接复用该接口的按需加载方案。
  不阻塞容量测量、能力快照及预算工作；底层逐 Mip 上传存在替代实现，整个版本并非无路可走。
- 通用性：任何采用 Texture Asset Manifest、仅读取所选 GPU 格式负载的引擎或查看器都可复用，
  不涉及 Gneiss 格式、路径、RID、场景或任务调度。

### 已核对的契约

`include/granit/renderer/texture_asset.h` 的 `granit_upload_batch_write_texture_asset_mips`
接收 Manifest、Payload 指针/长度、变体索引与 Mip 范围。
`src/renderer/texture_asset_api.cpp` 调用 `validate_texture_asset_payload` 后，使用
`variant.payload_offset + subresource.data_offset` 取数据。
`src/asset_formats/texture/texture_asset.cpp` 的验证器同样按原始 Payload 起点截取变体并检查 SHA-256。

因此当前参数必须覆盖所选变体在原始 Payload 中的偏移范围；不是必须包含其后的其他变体，
但调用方不能把非零偏移变体的独立缓冲当作基址。偏移为零的首个 BC7 变体可能直接工作，
不能据此认定 RGBA8 回退或任意变体顺序也已经支持。

### 最小复现条件

构造一个二维单层、单 Mip 纹理 Manifest：BC7 数据 16 B 位于偏移 0，RGBA8 数据 64 B 位于
偏移 16；两者摘要正确。选择 RGBA8 后仅读取其 64 B 数据。

当前验证器检查 `64 > 64 - 16`，判定范围不足；若传入完整的 80 B 原始负载，范围检查成立。
这是依据当前源码得到的确定性边界推导；本轮未执行该新夹具的真实 GPU 上传，不将它计作 GPU 复现。
上游实现时应使用 AssetTools 构造实际合法 Manifest 并完成下面的公共 API 回归。

### 建议最小 PR

建议新增 C/C++ 入口（名称由上游确定），参数与现有接口相近，但明确 Payload 指针指向
**所选变体的起点**，长度为该变体完整负载长度：

- 原 Manifest 和变体索引保持不变，不要求调用方改写 Manifest、调整偏移或填充未选中的前缀。
- 验证 Manifest、变体索引、完整变体长度和摘要；按变体局部偏移读取 subresource。
- 保留 Mip 范围、空 Batch 要求、预算背压及失败清理语义。长度不足、摘要不符等前置失败不得添加操作。
- 成功后不再借用调用方 Manifest/负载；提交、取消、销毁与完成行为沿用 Upload Batch。
- 保留旧 API 的基址语义，不能静默改变旧调用方行为；公共 C ABI、C++ 包装和文档同步提供。

本需求只涉及完整所选变体，暂不要求只传某几个 Mip 的局部负载。后者还需要明确摘要验证粒度，
不能在当前扩展中隐式绕过整变体摘要。

### 测试验收

1. 首个与后续变体、非零偏移 RGBA8 回退、多种变体顺序；独立缓冲与旧完整负载接口的 GPU 回读一致。
2. 错误索引、错误长度、损坏摘要、截断、溢出、错误 Mip 范围及目标纹理不匹配。
3. 预算不足返回 NOT_READY，重试可成功；前置错误时 Batch 仍空，写入失败符合既定重置契约。
4. 成功写入后释放 CPU 原缓冲再提交，验证不依赖调用者内存；取消、销毁及旧 API 回归。
5. 支持的后端执行对应测试；无 BC7 的设备仍可通过 RGBA8 路径验证非零偏移，不以缺少 BC7 跳过全部验收。

### Gneiss 接入与替代方案

后端提供设备格式能力快照，工作线程读取头与 Manifest，按能力选择变体并只读取其负载。
CPU 候选保存原 Manifest、变体索引和局部负载；渲染线程核对设备代次后调用新入口。
VFS/文件版本校验与 GPU 变体摘要是不同职责，两者均保留。

若不扩展上游，Gneiss 可通过公开 `inspect_texture_asset`、自身 SHA-256 和逐 Mip `write_texture`
完成同类操作，但需要维护重复的 Manifest 到上传映射、预算与失败处理。优先在 Granit 提供这个可复用入口，
不通过补零前缀、分配完整负载或修改上游私有结构规避。

## 无需上游新增的能力

当前 `get_texture_format_capabilities` 已能查询格式使用与特性；`select_texture_asset_variant`
已有按 Manifest 顺序选择的规则。Gneiss 应在合法后端线程获得纯数据快照并传给准备任务，
设备代次、缓存身份与重试由 Gneiss 实现。本轮不提出第二套上游任务调度或 Gneiss 专用选择服务。

## Gneiss 接入状态

上游新增独立变体入口并保留旧接口。Gneiss 渲染后端先检查所选范围，再将该范围作为局部负载传入新入口。
目前 CPU 资源仍保留原始全部负载；此改动验证上传接口，不能宣称按需加载或驻留优化已经完成。
后续设备能力快照、CPU 选择、仅读取所选负载和预算回收仍由 Gneiss 完成。

新增真实场景像素夹具：第一个 RGBA8 变体仅允许 transfer_destination，第二个允许采样且偏移非零。
两个负载内容不同，第二个含完整 Mip 链；运行中必须选择第二个并采样正确 Mip 才能通过像素断言。
Windows 本机完整 asset-pixel 开启 Vulkan 校验通过，无 Validation Error/VUID。
版本缓存迁移与 render_asset_loader 2/2 通过；后续平台检查见
[Granit 0.44 接入记录](../records/M-287-granit-0.44-integration.md)。
