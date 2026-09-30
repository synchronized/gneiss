<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-289：候选计数与上传硬上限

## 本轮范围

2026-09-30 修正文件纹理 CPU 负载释放后候选逻辑容量漏计的问题。
`resident_bytes` 保留 Manifest 与所选负载的逻辑容量；`cpu_data_bytes` 统计已提交 CPU 数据数组，
包含资源 Manifest 及重建来源保留的 Manifest；`texture_payload_bytes` 单独报告纹理负载。
按 RID 更新旧值，不因重复资产租约重复累加。候选逻辑容量及 CPU 数据分别受 2 GiB 上限约束。
这些都是数据尺寸，不是分配器容量、进程 RSS 或驱动显存。

上传维持 8 MiB 分批目标，新增 256 MiB 单次硬上限。超限单资产在调用后端前拒绝并给出所需/上限
字节；已有成功批次时走原回滚链路。接受后记录 `upload_reserved_bytes`，完成回执到达后清零，
并将估算峰值汇总到场景结果。尚未改成全 Application 共享的预算池。

场景基准 JSON 增加 `candidate_cpu_data_bytes`、`candidate_texture_payload_bytes` 和
`peak_upload_bytes`，保留原有字段，便于下一阶段大场景测量。

## 测试窗口

asset-pixel 的所有 Application 描述关闭窗口 visible 标志，继续创建真实 GPU Surface 并执行
完整像素回读；没有跳过渲染断言。窗口交互、resize、最小化专项测试不在此变更范围。

## 验证

- thread_pool / cooperative 两种模式验证上传超限前拒绝、无候选发布、失败后重试、
  在途预留、回执后清零及峰值报告。
- 异步真实 GPU 夹具精确核对释放后的 CPU 数据、所选纹理逻辑负载与 Manifest 副本。
  小纹理元数据可能大于负载，不能简单以 CPU 字节小于逻辑字节作为正确性条件。
- 无文件纹理的场景继续满足原有候选字节断言。

Windows Clang Debug 共享库全量构建通过，相关回归 9/9 通过（12.82 s）。静态库相关目标
构建通过，场景、纹理服务及隐藏窗口像素回归 3/3 通过（12.50 s）。两轮 GPU 回归均启用
`VK_LAYER_KHRONOS_validation`，原始日志无 Validation Error / VUID。
修正新增初始化列表告警后，受影响目标重新构建，共享场景及纹理服务 2/2 复验通过。

对两份服务实现执行变更行 clang-tidy；修正新增初始化列表告警，既有 `advance_impl` 函数
仍超过复杂度阈值，未将检查降级或宣称全仓无告警。`git diff --check` 通过。
日志位于本地 `build/0.44-texture-residency/budget-*.log`，未触发远端 Actions。

## 后续

全局预算池、活动/候选/暂存合计限制、共享来源及分配器开销、读取前完整预留、Editor 分项反馈仍待
完成；本轮不宣称 M-289 已完成。完整 4K Cook Sponza、Linux 与发布矩阵尚未验收。
