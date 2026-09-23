<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 远距离缩放冻结与无阴影投射物路径排查

后续状态：上游已在 0.29.1 发布修复，Gneiss 接入结果见[升级验收](2026-09-23-granit-0.29.1-upgrade.md)。
下文保留 0.28.1 排查时的结论。

## 结论

2026-09-23，在 Windows Clang Debug、Granit 0.28.1
（`201adc8a4e4ed205cecb5bd99e3b207eb2d99c84`）上复现远距离缩放后持续无法呈现新帧。
方向光阴影体积内没有投射物时，管线传递空阴影视图，后续不透明绘制返回
`GRANIT_ERROR_NOT_READY`。问题尚未修复，需要 Granit 修正通用管线行为。

进程仍响应系统消息，主循环继续执行，但 Gneiss 取消失败帧且不呈现，导致场景和 UI 一同冻结。
用户报告 resize 后黑屏；新交换链无法获得成功渲染内容可以解释该现象，但本轮没有独立完成
原生窗口 resize 复现，不能将此解释视为已验证结果。

## 复现与对照

临时延长 Editor smoke，并在 Lantern Gallery 项目每帧相机更新后注入 `dolly=-1`：

| 条件 | 主循环帧数 | 实际执行渲染 | 结果 |
| --- | --- | --- | --- |
| 保留方向光，前 1800 帧拉远、随后停止 | 2400 | 790 | 前 9 帧成功，从渲染序号 11 起连续 781 帧返回 -14 |
| 临时移除方向光，持续拉远 | 600 | 214 | 全部成功并呈现 |

渲染线程会替换尚未执行的待处理帧，因此主循环帧数与实际执行渲染数不同。
诊断日志保存在本地 `build/hang-run.log`、`build/hang-control-run.log`，不纳入版本控制。
资产监听的 `not found` 警告也出现于成功对照运行，不能用它解释此次渲染失败。

## 源码链路

以下路径相对于 Granit 源码根目录：

1. `src/lighting/directional_shadow.cpp` 以相机位置为中心构造方向光阴影体积；体积内没有
   可见投射物时返回 `directional_shadow_error::no_casters`。物体仍可能在相机视野内。
2. `src/pipeline/render_pipeline_api.cpp` 接受该结果，但执行 `shadow.reset()`，PBR 回调因而
   将 `GRANIT_NULL_HANDLE` 传给 `record_opaque_draws`。
3. `src/pipeline/forward_draw_recorder.cpp` 在处理空绘制列表和清屏之前检查阴影视图，
   空句柄直接返回 `GRANIT_ERROR_NOT_READY`。
4. Gneiss 将该错误视为暂不可用并重试；只要场景条件不变，每帧都会失败。

## 建议提交给 Granit 的 PR

建议标题：`fix: 无方向光阴影投射物时仍完成帧渲染`。

最小范围是 Render Pipeline 的 `no_casters` 分支、阴影采样资源及回归测试；不涉及 Gneiss
网格、资产格式或编辑器协议。无投射物是合法场景状态，必须正常完成清屏、可见物体光照、
后处理、Debug Draw 与 Canvas/UI，并允许提交和呈现。

建议使用已正确初始化的无阴影回退资源，或等效的无阴影采样路径，并保持 Render Graph
资源声明、绑定和采样常量一致。已有 placeholder 不能仅凭创建成功就视为内容有效；必须保证
初始化及同步正确。不能继续采样上一帧残留阴影，也不能只吞掉错误或永久关闭方向光。

建议验收：

- 有投射物 → 无投射物 → 有投射物，连续帧均成功，阴影恢复正确且没有残留。
- 相机仍可见物体，但阴影体积内没有投射物，保持方向光照和 UI。
- 空场景或仅 UI 时仍清屏、提交；在无投射物状态 resize 后继续呈现。
- 覆盖支持的渲染后端及验证层，确认资源状态与生命周期正确。

由用户向 Granit 提交修复；Gneiss 在上游版本可用后更新依赖，再验证真实滚轮拉远、停止输入、
拉回及窗口 resize 的端到端行为。

## 清理与验证边界

临时自动缩放、日志、smoke 帧数和方向光对照改动均已撤销，没有修改 Granit 源码。
正常 Clang Debug 编辑器重新构建通过；Camera、Grid、Editor smoke、Lantern Editor 共 4 项
测试通过，日志为本地 `build/hang-clean-build.log` 与 `build/hang-clean-tests.log`。
这些测试不覆盖上述长距离缩放缺陷，不能据此宣称冻结已修复。本轮未执行其他平台完整矩阵。
