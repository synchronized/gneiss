<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# UPSTREAM-043：Granit PBR 上游需求与接入跟踪

## 状态与规则

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

- 状态：待验证；P1，可能阻塞 MASK/doubleSided。
- 证据与边界：已检查 v0.39.0 [PBR Schema](https://github.com/synchronized/granit/blob/v0.39.0/include/granit/pipeline/pbr_material.h)
  与[标准 Shader](https://github.com/synchronized/granit/blob/v0.39.0/assets/sources/shaders/pbr/pbr_standard.hlsl)，
  未见公开 alphaMode/cutoff 参数和相应 discard；仍需查完整 Shader 变体、Pipeline 状态与双面法线支持。
  不能仅因 Gneiss 没有字段就断定上游所有路径均不支持。
- 如证实缺口，建议最小 PR：明确 OPAQUE/MASK、cutoff、剔除及背面法线语义；MASK 保持深度写入，
  若声明支持阴影则影子 Pass 使用同一遮罩；不与 BLEND 排序混为一项。
- 验收：阈值两侧、Alpha 因子/贴图组合、正反面照明、负缩放、深度和阴影轮廓。
- Gneiss 接入：Material 序列化与导入保留状态，通过公开材质/管线变体投影；不复制私有 Shader 布局。
- 上游链接：尚无；完成最小实验后再提出。

## U43-03：逐贴图 UV 与采样器

- 状态：待验证；P1，可能阻塞核心 glTF 的多 UV/异构 sampler 材质。
- 证据：上述 v0.39.0 Schema 暴露 UV0 和单个 `pbr_sampler`，标准 Shader 五种纹理共用一个 UV 与 sampler。
  需确认是否有其他公开标准路径，不能把创建任意自定义 Shader 视为标准材质已经支持。
- 如证实缺口，建议最小 PR：逐槽选择 UV0/UV1 与 sampler，保留单 UV/共享 sampler 默认行为，
  给出能力检查、缺失顶点属性错误及公共元数据契约。UV transform 扩展按真实资产需求另评估，不绑入最小 PR。
- 验收：不同 UV 图案、repeat/clamp/mirror、各槽不同过滤、缺 UV1、默认兼容、资源销毁与变体验证。
- Gneiss 接入：导入、Mesh/Material 格式和依赖保存选择，上传所需顶点属性；纹理资源可共用而采样状态独立。
- 上游链接：尚无；M-279 最小实验后更新。

## U43-04：负缩放下切线空间手性

- 状态：已提出（2026-09-29 会话中告知用户，未提交上游 Issue/PR）；P0，影响法线贴图负缩放验收。
  v0.39.0 标准 Shader 顶点阶段以 model 变换 tangent.xyz，
  但直接传递 tangent.w；片元阶段使用 `cross(N,T) * w` 重建 B，未乘 model 线性部分行列式的符号。
- 最小数学复现：N=(0,0,1)、T=(1,0,0)、w=+1，model=diag(-1,1,1)。变换后 N'=N、T'=(-1,0,0)，
  当前公式给出 B'=(0,-1,0)，而原始 B=(0,1,0) 经 model 后仍为 (0,1,0)。沿切线空间 +Y 倾斜的
  法线因而朝相反方向。此为源码和数学证据，尚未运行上游 GPU 复现。
- 建议最小 PR：标准 PBR 在每实例变换中正确合成几何反射与 UV 手性，并在插值后正交化 TBN；
  明确奇异矩阵的拒绝/降级契约。不能通过修改共享 Mesh 的 tangent.w 修复，因为同一网格可被正负缩放实例共用。
- 通用性：任意使用标准 PBR 的模型查看器和上层引擎均受益，无 Gneiss 专用数据。
- 验收：同网格正/负缩放实例、UV 镜像、两次反射、非均匀缩放和旋转，使用非平法线贴图；
  CPU 参考与 GPU 像素对照，同时验证正面判定/剔除语义，不把对象被剔除当成通过。
- Gneiss 接入：保留导入切线手性与实体变换，升级上游标准资产后运行负缩放图像夹具；不分叉上游 Shader。
- 上游链接：尚无；用户提交后补充。

## Gneiss 自行完成的工作

五类贴图默认绑定的替换、切线导入/生成、颜色空间与 Mip、格式版本、依赖与缓存、打包、异步事务、
Editor 诊断及场景材质转换属于 Gneiss。本轮未证实这些需要上游新增能力，不应整体下沉为 Granit Import Service。
顶点色及其他契约在 M-279 能力审计中逐项核验，发现实际底层缺口时按上述字段新增条目。
