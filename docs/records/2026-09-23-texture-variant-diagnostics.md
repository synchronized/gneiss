<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 运行纹理诊断与 Lantern Cook 验收

- 日期：2026-09-23
- 范围：0.35.0 的 M-235，Granit 0.28.0，Windows MSVC Shared Release。
- 关联：[实施计划](../plans/VER-035-0.35.0-texture-asset-variants.md)、
  [当前纹理检查与日志行为](../reference/render-asset-formats.md#texture)。

## 已完成

- CLI 支持 `.gneiss-texture` 的 `inspect` 与 `validate`。复用 Gneiss 封装解码和 Granit Manifest
  检查，校验二维完整 Mip 链、颜色空间、RGBA8 回退、负载边界及全部变体 SHA-256。
- 渲染线程通过 Application 的有界日志队列记录 RID、设备选择、Mip 数、上传字节数及失败阶段；
  用户日志回调继续在消费线程执行，日志队列生命周期覆盖渲染执行器停机。
- 新增 `gneiss.runtime.lantern-cooked-textures`：Cook 灯廊资产、检查双变体及无作者 PNG，启动真实
  图形后端，再检查本次生成的日志。测试随后破坏灯笼基础颜色纹理的全部变体摘要，验证失败路径，
  并恢复原文件。重复执行前仅清理约定的构建测试目录。
- 负载损坏验收发现短程主循环可能在异步上传错误回传前返回成功。现正常结束主循环前等待已提交帧
  完成渲染线程处理、回收完成结果；可重试的 NOT_READY 仍不作为永久失败，不等待 GPU 执行完成。

## 结果

| 检查 | 结果 |
| --- | --- |
| MSVC 严格警告构建 | 通过；修正 `bit_width` 的有符号比较，未放宽检查 |
| 初轮纹理、CLI、日志、平台及 Lantern 相关测试 | 21/21 通过 |
| 主循环修正后的全量测试 | 139/141 首次通过；两个失败项复测通过，详见下文 |
| 新增检查器及损坏夹具的 clang-tidy | 自有代码无未抑制告警；使用现有 Clang 编译参数 |
| 修改源码 clang-format、提交范围 diff 检查 | 通过 |

真实后端日志确认灯笼基础颜色纹理选择 `variant=0 format=BC7_SRGB`，共 10 级 Mip、349,552 字节，
`stage=ready result=0`。破坏负载后，离线校验拒绝摘要不匹配；Runtime 记录 `stage=write result=-2`，
主循环返回错误，进程退出码为 7。该结果验证选择、提交和错误传播，不等同于最终画面像素验收。

全量首轮失败分别为：新增 Lantern 测试重复运行时 Cook 输出目录已存在，以及无诊断输出的
`gneiss.runtime.ipc-session`。前者已添加受路径检查保护的测试目录清理，并复测成功；后者未经代码
或超时改动单独复测通过，首次失败原因仍未定位。未将这次结果表述为完整矩阵首轮全绿。

## 未覆盖

本次真实设备选择 BC7；未用不支持 BC7 的真实设备复验 RGBA8 回退。Linux、Sanitizer、完整跨编译器
及 Shared/Static 矩阵、真实桌面交互与发布仍属于后续验收，本记录不构成 0.35.0 发布通过。
