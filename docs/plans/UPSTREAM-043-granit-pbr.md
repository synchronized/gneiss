<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# UPSTREAM-043：Granit PBR 上游需求与接入跟踪

## 状态与规则

2026-09-30 修复接入：用户通过 [Granit PR #122](https://github.com/synchronized/granit/pull/122)
修复 U43-06/07，已发布于 v0.43.0。Gneiss 当前锁定
`555b1144b02be409c74b7cc971eff2e0ecf74e25`，Windows 实机与 Linux 共享/静态 Lavapipe 的完整像素回归已通过，开启校验层无 VUID 错误。
下面旧版本缺陷与复现保留为历史证据；最终结果见 [M-284～M-285](../records/M-284-285-0.43.0-validation.md)。

2026-09-29 接入更新：上游 [PR #117](https://github.com/synchronized/granit/pull/117) 已实现下列
五项需求，包含在当前最新正式版 [v0.42.0](https://github.com/synchronized/granit/releases/tag/v0.42.0)。
Gneiss 已锁定 `29a4f18a67a8f506c585f0f515d93ddc406d7426`，适配五个独立采样器参数与新顶点布局校验。
U43-04 的负缩放探针已进入普通 CTest 并通过：不再反转索引，正负实例均可见，法线像素
为 `(172,168,165)` / `(172,168,166)`。RGBA8 的 128 解码为 +1/255，反射 X 会改变该残余分量，
故允许每通道两个量化级；此前约 80 级的错误无法通过。升级针对性 5/5 通过。
U43-01/02/03/05 已接通数据、导入与 GPU。小夹具通过 MASK 阈值、双面可见性、顶点颜色/Alpha、
UV1、clamp/mirror、BLEND Alpha 0/0.5、两层透明顺序及不透明遮挡像素检查。
Windows 本地共享/静态回归、日常 Sponza 重复加载与交互通过；完整 4K 明确预算拒绝并回收。
2026-09-30 已将 Linux 崩溃定位到 MASK 首帧；本地 Vulkan 校验进一步确认 U43-06/07 两处
上游契约错误，详见下文历史证据。当时暂停发布；随后用户修复，接入结果以本页首段为准。
接入见 [M-283](../records/M-283-granit-0.42-material-states.md)，最新门禁见
[M-284～M-285](../records/M-284-285-0.43.0-validation.md)。
以下需求及 v0.39.0 状态/复现证据为历史记录，以本段接入状态为准。

2026-09-29 初次核对，基线为 Granit v0.39.0，目标版本见 [VER-043](VER-043-0.43.0-pbr-pipeline.md)。
本页是需求与协调计划，不表示已向上游提交 Issue/PR。用户负责向 Granit 提交；代理不修改上游仓库。
发现新阻塞时立即更新本页并向用户提出，不等版本结束才汇总。

每项记录：复现版本/证据、用户场景、通用性、建议最小 PR、接口行为、测试、Gneiss 接入方式、
优先级、是否阻塞、状态和上游链接。状态使用“待验证、待提出、已提出、上游实现中、待接入、已验收、暂缓”。
只有公开契约核验和最小复现证实缺口，才把待验证项升级为上游实现需求。

## U43-01：标准透明 PBR Pass

- 状态：已提出（2026-09-29 会话中告知用户，未提交上游 Issue/PR）；P1，阻塞 Gneiss BLEND 材质闭环，不阻塞不透明五贴图链路。
- 证据：上游 [H-09B](https://github.com/synchronized/granit/blob/v0.39.0/docs/plans/H-09B-transparent-pbr-correctness.md)
  明确实现暂缓，当前只确认评估契约。Sponza 含透明材质，0.42 转换损失见
  [资产基线](../records/M-272-sponza-baseline.md)。
- 通用性：受光透明模型可被多个上层引擎/查看器复用，不携带 Gneiss 场景或 RID 语义。
- 建议最小 PR：依据已有 H-09B 实现标准 PBR BLEND 变体与 HDR 透明阶段，公开可提交的材质/绘制契约。
  复用不透明光照公式与资源，透明位于不透明之后、Tone Mapping 之前；深度测试开、深度写入关，
  线性 HDR 预乘 Alpha，按 View 稳定远到近排序。明确排序由调用方还是 Pipeline 承担，并只由一处负责。
  不要求折射、OIT 或透明阴影；对象内相交排序限制应公开。
- 验收：两层/八层、等深度、多 View、Alpha 0/1、不透明遮挡、光照/IBL、一致曝光及失败/资源生命周期；
  各支持后端验证混合和深度行为，不支持时显式返回能力错误。
- Gneiss 接入：保留 glTF alphaMode/factor，分类渲染提交并传递稳定身份；透明对象仍由 Gneiss 管理，
  GPU 阶段使用 Granit 公共接口，配合小夹具与 Sponza 端到端验证。
- 上游链接：尚无；用户提交后补充。

## U43-02：标准 PBR 镂空与双面契约

- 状态：已提出（2026-09-29 会话中告知用户）；P1，阻塞 MASK/doubleSided 及完整 Sponza 重导入。
- 证据与边界：已检查 v0.39.0 [PBR Schema](https://github.com/synchronized/granit/blob/v0.39.0/include/granit/pipeline/pbr_material.h)
  与[标准 Shader](https://github.com/synchronized/granit/blob/v0.39.0/assets/sources/shaders/pbr/pbr_standard.hlsl)，
  标准 Shader 库仅有该标准路径，Schema 无 alphaMode/cutoff，Shader 无 discard 或背面法线翻转。
  自定义 Shader/通用剔除状态不能替代标准 PBR 的完整材质契约。实际 Sponza 中 `lamp_glass_01` 与
  `glass` 声明双面，新导入检查返回明确的 U43-02 诊断；尚未运行对应上游 GPU 实验。
- 建议最小 PR：明确 OPAQUE/MASK、cutoff、剔除及背面法线语义；MASK 保持深度写入，
  若声明支持阴影则影子 Pass 使用同一遮罩；不与 BLEND 排序混为一项。
- 验收：阈值两侧、Alpha 因子/贴图组合、正反面照明、负缩放、深度和阴影轮廓。
- Gneiss 接入：Material 序列化与导入保留状态，通过公开材质/管线变体投影；不复制私有 Shader 布局。
- 上游链接：尚无；用户提交后补充。

## U43-03：逐贴图 UV 与采样器

- 状态：已提出（2026-09-29 会话中说明导入限制）；P1，阻塞核心 glTF 的多 UV/异构 sampler 材质。
- 证据：上述 v0.39.0 Schema 暴露 UV0 和单个 `pbr_sampler`，标准 Shader 五种纹理共用一个 UV 与 sampler。
  标准 Shader 库仅有该路径；创建任意自定义 Shader 不等于标准材质已支持。
  Gneiss 当前明确接受 UV0、repeat/linear/trilinear，对非默认状态和 UV1 有拒绝测试。
- 建议最小 PR：逐槽选择 UV0/UV1 与 sampler，保留单 UV/共享 sampler 默认行为，
  给出能力检查、缺失顶点属性错误及公共元数据契约。UV transform 扩展按真实资产需求另评估，不绑入最小 PR。
- 验收：不同 UV 图案、repeat/clamp/mirror、各槽不同过滤、缺 UV1、默认兼容、资源销毁与变体验证。
- Gneiss 接入：导入、Mesh/Material 格式和依赖保存选择，上传所需顶点属性；纹理资源可共用而采样状态独立。
- 上游链接：尚无；用户提交后补充，逐槽 GPU 验收待契约实现。

## U43-04：负缩放下切线空间手性

- 状态：已提出（2026-09-29 会话中告知用户，未提交上游 Issue/PR）；P0，影响法线贴图负缩放验收。
  v0.39.0 标准 Shader 顶点阶段以 model 变换 tangent.xyz，
  但直接传递 tangent.w；片元阶段使用 `cross(N,T) * w` 重建 B，未乘 model 线性部分行列式的符号。
- 最小数学复现：N=(0,0,1)、T=(1,0,0)、w=+1，model=diag(-1,1,1)。变换后 N'=N、T'=(-1,0,0)，
  当前公式给出 B'=(0,-1,0)，而原始 B=(0,1,0) 经 model 后仍为 (0,1,0)。沿切线空间 +Y 倾斜的
  法线因而朝相反方向。
- Gneiss 最小 GPU 复现（Windows / Vulkan / Intel UHD 630）：运行
  `gneiss_asset_pixel_test --probe-negative-scale`，v0.39.0 返回 2。
  正负缩放自发光可见性控制都为 RGB `(236,236,237)`；同一 +Y 法线分别为 `(172,168,165)` 和
  `(91,78,70)`。夹具仅反射 X，理论 B 应保持 +Y；负缩放夹具反转索引绕序以隔离 TBN，
  不是生产修复，也不代表负缩放剔除已正确支持。未抵消绕序时负缩放对象不可见。
  探针不加入普通通过计数，属于尚未通过的发布门禁；源码见 `tests/render/asset_pixel_test.cpp`。
- 建议最小 PR：标准 PBR 在每实例变换中正确合成几何反射与 UV 手性，并在插值后正交化 TBN；
  明确奇异矩阵的拒绝/降级契约。不能通过修改共享 Mesh 的 tangent.w 修复，因为同一网格可被正负缩放实例共用。
- 通用性：任意使用标准 PBR 的模型查看器和上层引擎均受益，无 Gneiss 专用数据。
- 验收：同网格正/负缩放实例、UV 镜像、两次反射、非均匀缩放和旋转，使用非平法线贴图；
  CPU 参考与 GPU 像素对照，同时验证正面判定/剔除语义，不把对象被剔除当成通过。
- Gneiss 接入：保留导入切线手性与实体变换，升级上游标准资产后运行负缩放图像夹具；不分叉上游 Shader。
- 上游链接：尚无；用户提交后补充。

## U43-05：标准 PBR 顶点色输入

- 状态：已提出（2026-09-29 会话中告知用户）；P1，阻塞原始 Sponza 的完整重导入。
- 证据：v0.39.0 标准 Shader 的顶点输入包含 position/normal/tangent/UV，不包含 COLOR；
  原始 Sponza 的 59 个 Primitive 声明 COLOR_0。Gneiss 现明确诊断此特性，不删除属性后声称保真导入。
- 通用性：glTF 核心材质和多个模型查看器均可使用，不携带 Gneiss 场景语义。
- 建议最小 PR：标准 PBR 可选线性 RGBA 顶点色，与 base-color 贴图及因子相乘；缺失时为白色。
  公共顶点布局/变体契约应允许无颜色的旧网格保持兼容，不能要求上层猜测私有布局。
- 验收：RGB/Alpha 各通道、无颜色默认、插值、OPAQUE/MASK/BLEND 中 Alpha 只组合一次。
- Gneiss 接入：保存 COLOR_0，扩展版本化网格属性和上传布局；顶点色不烘焙进共用材质纹理。
- 上游链接：尚无；用户提交后补充。GPU 验收待上游契约实现。

## Gneiss 自行完成的工作

五类贴图默认绑定的替换、切线导入/生成、颜色空间与 Mip、格式版本、依赖与缓存、打包、异步事务、
Editor 诊断及场景材质转换属于 Gneiss。本轮未证实这些需要上游新增能力，不应整体下沉为 Granit Import Service。
本轮另修复 Gneiss 自身的 Mip 采样限制：采样器最大 LOD 与纹理 View 的层数都必须开放完整链；
不要求 Granit 修改已正确公开的采样器和 View API。

## U43-06：MASK 阴影顶点阶段的材质常量可见性

- 状态：已验收（Granit 0.43.0 / PR #122，Windows 与 Linux 校验层通过）；P0。建议 PR 目标为 Granit `main`，
  最小范围是修正标准 MASK 阴影 Shader 与材质布局的阶段契约，并补验证层回归。
- 复现版本：v0.42.0，SHA `29a4f18a67a8f506c585f0f515d93ddc406d7426`。
  Gneiss [Linux 诊断矩阵](https://github.com/synchronized/gneiss/actions/runs/36595776652)
  的共享/静态组都在首个 MASK Alpha 0.25 用例崩溃，OPAQUE 和五槽通道此前通过。
  GDB 显示软件 Vulkan 工作线程 SIGSEGV，提交线程在等待 GPU 完成；仅凭该栈不足以归因。
- 确认依据：Windows / Intel UHD 630 / Vulkan SDK 1.4.321.1 开启校验层，报
  `VUID-VkGraphicsPipelineCreateInfo-layout-07988`：Set 1 / Binding 0 的 MaterialConstants
  在顶点阶段读取，布局 `stageFlags` 却仅有 FRAGMENT。
- 源码对应：`assets/sources/shaders/pipeline/shadow_depth.hlsl` 的 `mask_vertex_main`
  在 UV1 变体读取 `uv1_mask`；`src/material/material_template_gpu.cpp` 的材质常量布局
  固定为 `GRANIT_SHADER_STAGE_FRAGMENT_BIT`。Gneiss 的 72 字节顶点布局启用 UV1/Color
  变体，即使当前槽选 UV0，也会使用该 Shader 分支；这是合法输入，不能靠隐藏属性回避。
- 建议修复：统一常量的阶段可见性，或让阴影顶点输出两个 UV，在片元阶段读取材质并选择 UV。
  由上游选择符合长期布局契约的方案，不要求修改 Gneiss 资产格式或复制私有 Shader。
- 接口行为：合法的 MASK + UV1/Color 组合应能创建、绘制与回收；无 Vulkan validation error。
  不能依靠驱动容忍非法布局，不能将崩溃改成静默丢失阴影。
- 验收：UV0/UV1、无色/顶点 Alpha、cutoff 两侧、镜像/负缩放和纹理 Alpha，检查阴影轮廓及回收；
  至少在 Linux Lavapipe 和一台真实 GPU 上启用校验层。修复前应复现该 VUID，修复后必须消失。
- 通用性：标准 MASK 投影被所有 Granit PBR 调用方复用，不涉及 Gneiss RID、场景或编辑器语义。
- Gneiss 接入：升级到含修复的固定 Granit 提交，保留现有 UV1/Color 布局，复跑像素测试与 Linux
  共享/静态运行时。用户负责上游提交，Gneiss 本 PR 不修改上游源码。

## U43-07：Shader Demote 能力与 Vulkan 设备特性不一致

- 状态：已验收（Granit 0.43.0 / PR #122，Windows 与 Linux 校验层通过）；P0。可与 U43-06 同一修复 PR 提交，
  但应独立验收；不声称它是本次段错误的唯一原因。
- 证据：相同探针报 `VUID-VkShaderModuleCreateInfo-pCode-08740`，SPIR-V 声明
  `DemoteToHelperInvocation`，设备却未启用 `shaderDemoteToHelperInvocation`。
  `src/backend/vulkan/device.cpp` 的 Vulkan 1.3 特性只启用 synchronization2、dynamicRendering、
  maintenance4，未启用该位。完整像素测试也出现此错误，不限于一个 MASK 实例。
- 最小范围：核对 Shader 编译目标和设备能力协商。若生成代码依赖 demote，则查询硬件支持后明确启用；
  不支持时在创建阶段返回明确错误或采用不依赖该特性的合法 Shader 变体。不能未经查询直接置 true。
- 测试：支持/不支持特性两条路径，标准 Shader 的能力检查，MASK/阴影绘制无 08740；
  保留现有功能、图像断言和错误语义，不仅检查进程退出码。
- 通用性：这是 Vulkan Shader 与设备能力协商，适用于所有使用相关 SPIR-V 的上层项目。
- Gneiss 接入：随固定依赖升级验证，不在 Gneiss 私自重编/改写 Granit 标准 Shader 来绕过。

### 两项需求的最小复现

Gneiss `feat/0.43-pbr-pipeline` 已提供 `gneiss_asset_pixel_test --probe-mask`：一个三角形，
Material v5、MASK Alpha 0.25/cutoff 0.5、72 字节 UV1/Color 顶点、默认标准方向光与阴影。
无第三方模型依赖。它只用于定位，不能替代完整像素验收；Windows 可能退出 0 但仍有 VUID，
不能将退出成功视为 Vulkan 使用合法。

```powershell
cmake --build --preset windows-clang-debug --target gneiss_asset_pixel_test
$env:VK_INSTANCE_LAYERS = 'VK_LAYER_KHRONOS_validation'
& build/windows-clang-debug/bin/gneiss_asset_pixel_test.exe --probe-mask
Remove-Item Env:VK_INSTANCE_LAYERS
```

Linux 使用启用 Granit 的 `linux-clang-release` 构建，在已安装 validation layers 的窗口环境下：

```sh
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation xvfb-run -a \
  build/linux-clang-release/bin/gneiss_asset_pixel_test --probe-mask
```

精简原始输出见[校验日志](../records/artifacts/0.43-mask-vulkan-validation.log)。
