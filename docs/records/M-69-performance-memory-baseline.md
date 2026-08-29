<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-69：性能与内存基线记录

## 结论

稳定运行时样例已建立首份可重复的 Windows Clang Release 基线。固定环境完成 1 次进程预热和
10 次有效采样，每个进程先预热 60 帧、再采样 300 帧。当前数据用于记录数量级和识别波动，不作为
自动失败阈值。

## 环境

| 项目 | 值 |
| --- | --- |
| 操作系统 | Windows 10 x64 |
| CPU | Intel Core i5-8600K |
| GPU | Intel UHD Graphics 630 |
| GPU 驱动 | 31.0.101.2140；`vulkaninfo` 报告 101.2140 |
| 编译器 | Clang 22.1.8 |
| 构建 | Release、Shared、Granit Fetch |
| Gneiss | `08aab2aa7c8f120cde19bbca29ef219afcd60b04` |
| Granit | `d5aa1cceef0741c17ff58eac5f14f731a3991bcb` |

## 汇总

下表的“中位数”和“P95”均基于 10 个独立进程样本；稳定帧行先在每个进程内汇总 300 帧，再跨进程
汇总。

| 指标 | 中位数 | P95 | 最小值 | 最大值 |
| --- | ---: | ---: | ---: | ---: |
| Application 创建 | 121.424 ms | 129.807 ms | 108.949 ms | 129.807 ms |
| 场景与资产加载 | 8.803 ms | 10.401 ms | 8.197 ms | 10.401 ms |
| 输入与节点设置 | 0.499 ms | 0.591 ms | 0.481 ms | 0.591 ms |
| 稳定帧中位数 | 15.865 ms | 15.967 ms | 15.634 ms | 15.967 ms |
| 稳定帧 P95 | 29.853 ms | 30.617 ms | 28.245 ms | 30.617 ms |
| 360 帧运行阶段 | 5969.594 ms | 6417.952 ms | 5953.076 ms | 6417.952 ms |
| 场景卸载 | 0.017 ms | 0.070 ms | 0.015 ms | 0.070 ms |
| Application 销毁 | 73.321 ms | 560.444 ms | 64.880 ms | 560.444 ms |
| 进程墙钟时间 | 6281.269 ms | 6754.836 ms | 6246.847 ms | 6754.836 ms |
| 峰值常驻内存 | 134.12 MiB | 134.23 MiB | 134.04 MiB | 134.23 MiB |

完整环境、10 个原始样本及机器汇总见
[M-69 Windows Clang Release 原始数据](data/M-69-windows-clang-release.json)。

## GPU 逻辑资源检查接入后复测

Granit 资源统计接入后，在同一硬件、驱动、编译器和采样参数下再次完成 1 次预热与 10 次有效采样。
复测使用 Gneiss `f3eb62e3138e71fee0d10d223e444e4fff64e9d5` 和 Granit
`a126b5f719ef48215e02a190bb1b4c5b3e5708e8`。完整样本见
[M-69 资源统计接入后原始数据](data/M-69-windows-clang-release-after-resource-stats.json)。

| 指标 | 首次中位数 | 接入后中位数 | 判断 |
| --- | ---: | ---: | --- |
| Application 创建 | 121.424 ms | 117.692 ms | 同一数量级 |
| 稳定帧中位数 | 15.865 ms | 15.846 ms | 基本不变 |
| 稳定帧 P95 | 29.853 ms | 30.232 ms | 基本不变 |
| Application 销毁 | 73.321 ms | 89.477 ms | 增加约 16.2 ms，需继续复核 |
| 进程墙钟时间 | 6281.269 ms | 6292.953 ms | 基本不变 |
| 峰值常驻内存 | 134.12 MiB | 134.21 MiB | 基本不变 |

两轮采样的销毁阶段都各有一次明显离群值，首次为 560.444 ms，接入后为 634.086 ms。资源统计查询
本身为只读 Registry 快照，但当前样本不足以把中位数变化或离群值归因于查询、驱动同步或系统调度；
独立设备复测前不设置销毁耗时阈值。

## 判断与限制

- 稳定帧中位数的跨进程范围为 15.634～15.967 ms，当前环境主要受呈现节奏影响，不能直接解释为
  CPU 单帧成本。
- Application 销毁阶段有 1 次 560.444 ms 离群值，约为跨样本中位数的 7.64 倍；需在其他环境和
  更多轮次中确认是驱动/系统调度抖动还是可优化的同步等待。
- 峰值常驻内存由外部进程每 10 ms 采样，适合观察回归数量级，不等同于精确分配峰值或 GPU 内存。
- 当前仅覆盖一台 Windows 设备；在 Linux Sanitizer、另一台 GPU 和重复批次通过前不设置失败阈值。

## 后续

1. 手动运行 Linux Sanitizer 图形任务，验证 CPU 内存错误、未定义行为和退出泄漏。
2. 如发布前可获得另一套真实 GPU/驱动环境，重复 Release 采样并复核销毁阶段离群值。
3. 在该独立环境验证 GPU 逻辑资源退出检查，确认另一后端或驱动也能在正常退出时归零。
4. 证据稳定后单独评审回归阈值，避免把当前机器的呈现节奏固化为跨平台承诺。

首次 Linux LSan 图形运行定位到 Mesa 软件 Vulkan ICD 在 `vkEnumeratePhysicalDevices` 中保留的
240 字节进程级缓存，其中两个间接分配在 ICD 卸载后只能显示为未知模块。项目不使用宽泛抑制：
Application 与场景故障测试继续启用严格 LSan，图形样例运行 ASan/UBSan 并暂时关闭 LSan。GPU
逻辑资源退出检查已完成接入：Gneiss 将 Granit 锁定到
`a126b5f719ef48215e02a190bb1b4c5b3e5708e8`，并在 Renderer 销毁前
按依赖逆序释放所有自有 GPU 子资源。随后通过后端无关统计要求 `total_live_count == 0`；若未归零，
Application 销毁返回 `invalid state`，并由 `granit.render.resources` 诊断列出各资源类型数量。
`pending_retirement_count` 表示等待 GPU 安全点的后端对象，只记录诊断，不作为调用方泄漏。

Windows Clang Debug 图形构建的 70 项测试全部通过，包含 Granit Platform Smoke、稳定运行时、Temple、
Lantern Gallery 和隔离安装 Consumer，证明当前正常退出路径的逻辑资源计数均已归零。独立 GPU/驱动
环境复测保留为发布前建议项；当前没有第二台真实设备，因此不将它作为 M-69 完成阻塞条件。

最终 [Linux 手动矩阵](https://github.com/synchronized/gneiss/actions/runs/33224882920) 已通过：Clang/GCC
Shared/Static、Granit Shared/Static 共 6 项通过，GCC Sanitizer 任务中的 Application、场景故障和
稳定运行时图形检查也全部通过。

## 故障诊断矩阵

| 故障 | 结果与诊断 | 恢复检查 |
| --- | --- | --- |
| Application 参数无效 | `invalid argument`；`application/application.configuration` | 随后可正常创建 Application |
| 资产根不存在 | `not found`；`asset/asset` | 输出句柄为空，随后可挂载有效资产根 |
| 场景文件缺失 | `not found`；`asset/scene.load`，消息包含 URI | 实体数保持为零 |
| 场景 JSON 损坏 | `invalid argument`；`asset/scene.load`，消息包含 URI | 随后可创建并卸载空场景 |
| 构建未启用 Granit | `unsupported`；`backend/granit.platform` | 输出句柄为空 |
| Vulkan ICD 不存在 | 稳定样例报告“创建 Application”，结果 `unsupported` | 仅影响注入环境变量的进程 |

前三类创建故障在 Application 句柄尚未产生时使用空句柄回调；场景失败保持输出 Scene Instance 为空。
Linux Sanitizer 任务通过不存在的 `VK_ICD_FILENAMES` 自动验证设备初始化失败日志。
