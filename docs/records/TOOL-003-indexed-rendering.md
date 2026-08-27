<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# TOOL-003 索引渲染实施记录

## 结论

2026-08-27 在 Windows Clang Debug、Granit Vulkan 路径上完成索引渲染闭环。Lantern 的三个 Mesh
从 16,182 个展开顶点变为 4,145 个唯一顶点和 16,182 个 UInt32 索引，画面冒烟结果保持正常。

## 数据

| 指标 | 展开 Triangle List | Indexed Draw | 变化 |
| --- | ---: | ---: | ---: |
| 每帧变换顶点 | 16,182 | 4,145 | -74.4% |
| 动态几何上传 | 906,192 B | 296,848 B | -67.2% |
| Scene 与资产加载 | 113.0 ms | 85.1 ms | -24.7% |
| 三帧 Smoke 总阶段 | 约 0.62 s | 约 0.36 s | 约 -42% |

Indexed Draw 上传量包含 232,120 B 顶点和 64,728 B UInt32 索引。时间数据来自同一开发机的单次
Debug 诊断，只用于发现量级变化，不代表跨平台基准。

## 验证范围

- 当前 C ABI `struct_size` 校验，以及索引所有权复制。
- 索引数量、越界和保留字段失败路径。
- Mesh Binary Loader 保留唯一顶点和索引。
- Granit Index Buffer、Indexed Draw、Temple 与 Lantern Smoke Test。
- 默认构建、Granit 构建、公共头和安装后 Consumer。
