<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-286：纹理容量与加载所有权初审

## 状态与方法

2026-09-30 完成首轮资产容量统计与源码审计，M-286 尚未完成。
输入为 0.43 导入的 `0.43-full-assets` 和 `0.43-daily-assets-2`，每组目录含 72 张 PNG。
逐文件读取 IHDR 并以分块 SHA-256 固定身份；未进行完整 PNG 解码验证。
工具为 [texture_memory_inventory.py](../../tools/performance/texture_memory_inventory.py)，命令：

```powershell
python tools/performance/texture_memory_inventory.py <资产目录>/textures --output <新报告路径>.json
python -m unittest discover -s tools/performance -p test_texture_memory_inventory.py
```

结果为[完整配置](artifacts/0.44-full-texture-inventory.json)和
[日常配置](artifacts/0.44-daily-texture-inventory.json)。逐文件路径、尺寸、哈希与计算结果保存在 JSON。
工具按目录文件计数，不进行场景依赖遍历或实例去重。

## 容量结果

| 项目 | 完整配置字节 | 日常配置字节 | 性质 |
| --- | --- | --- | --- |
| PNG 文件总量 | 2,031,165,435 | 130,417,828 | 文件大小实测 |
| RGBA8 基础层 | 4,831,838,208 | 301,989,888 | 按尺寸计算 |
| RGBA8 完整 Mip | 6,442,450,848 | 402,653,088 | 按尺寸计算 |
| BC7 完整 Mip | 1,610,614,656 | 100,665,216 | 按块尺寸计算 |
| BC7 加 RGBA8 完整 Mip | 8,053,065,504 | 503,318,304 | 双变体负载估算 |

完整配置的 BC7 负载约 1.50 GiB，两种变体合计约 7.50 GiB。因此仅保留所选变体有明确收益空间；
1.50 GiB 不包含网格、材质、容器、对齐、CPU 副本和 GPU 分配开销，不能据此认定完整场景必然能
进入 2 GiB 候选预算。这里没有生成完整 Cook 产物，也没有测量 GPU 驱动驻留或新的运行峰值。
上一版本的实际加载和预算拒绝证据仍以 [M-284～M-285](M-284-285-0.43.0-validation.md) 为准。

## 当前所有权与读取路径

| 位置 | 已确认行为 | 接入影响 |
| --- | --- | --- |
| `src/asset/file_system.h` | 只有 `read` 和整文件 `read_bounded` | 尚无通用区间读取契约 |
| `src/asset/native_file_system.cpp` | 检查大小后分配整文件 vector | 限额约束单次读取，不等于进程总预算 |
| `src/asset/source_revision_file_system.cpp` | 读取后全文件哈希，发布前再次整文件读取校验 | 区间读取还需保持来源一致性，不能绕过验证 |
| `src/render/render_asset_loader.cpp` | 完整读取封装，再复制 Manifest 与全部 Payload | 文件缓冲和资源负载在准备阶段重叠 |
| `src/render/render_resource_service.cpp` | 资源通过不可变 shared_ptr 保存 | CPU 数据释放需处理帧、缓存及资源表的共同所有权 |
| `src/render/granit/granit_render_service.cpp` | 在渲染后端选择变体，仅该变体上传；上传统计另有按全部 payload.size 计数路径 | 准备选择时机和上传计量必须一起审计 |
| `src/tooling/asset_build/runtime_texture_builder.cpp` | 构建 BC7 与 RGBA8 两种完整 Mip 负载 | 当前问题并非缺少 BC7 编码器 |

不能只在上传后减少候选计数，也不能只将文件读取改为 seek：前者可能仍持有共享负载，后者可能
破坏准备与发布之间的内容校验。需要按 [ADR-052](../decisions/ADR-052-texture-residency-and-budgets.md)
定义设备选择、稳定来源、预算预留与生命周期。

## 验证与剩余工作

新增工具单元测试 2/2 通过，覆盖非方形、非整块 Mip、零尺寸、空目录、IHDR 校验错误与截断。
完整/日常配置各生成一次容量报告；这不计作三次运行加载样本，也不是端到端画质验收。

后续仍需：分阶段运行内存采样、实际 Cook 产物对照、设备能力与上传完成契约审计、可获取的 GPU
统计范围确认，以及活动资源/暂存预算和画质阈值冻结。本轮不宣称这些已经完成。
