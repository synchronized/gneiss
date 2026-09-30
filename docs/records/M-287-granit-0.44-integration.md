<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-287：Granit 0.44 独立变体上传接入

## 范围

2026-09-30 接入用户已完成的 [Granit PR #125](https://github.com/synchronized/granit/pull/125)。
固定依赖使用正式版 0.44.0 的 `369adc7ebc9e202056c5d583bfd8d65243de6a4d`，
同步 Fetch 默认值、旧缓存迁移、Package 与安装导出最低版本、编译期断言及构建指南。
Gneiss 自身版本仍为当前已发布基线，不将依赖升级视为 Gneiss 0.44 已发布。

渲染后端在设备选择后检查范围，将所选变体的子区间传入
`write_texture_asset_variant_mips`；Manifest 与变体索引保持不变。
CPU 资源此时仍保存完整负载，不能据此声称 CPU 驻留已缩减。

## 像素验收

asset-pixel 新增非零偏移夹具，首个变体无 sampled usage，强制选择第二个 RGBA8 变体。
首个负载全零，第二个包含高分辨率蓝色和较低分辨率红色 Mip；缩小采样必须读到红色。
这覆盖实际 Gneiss Loader → Granit 设备选择 → 局部负载上传 → 材质采样 → 回读链路。

Windows 本机启用 Vulkan SDK 1.4.321.1 的 `VK_LAYER_KHRONOS_validation`，完整像素回归通过，
日志无 Validation Error/VUID。原始日志位于本地
`build/0.44-texture-residency/granit044-pixel-validation.log`。
旧缓存升级和渲染资产加载测试 2/2 通过。

## 构建与平台回归

本地 Windows Clang Debug 共享/静态全量构建通过。共享完整 CTest 首轮为 161/164：
安装后运行及 Runtime 进程测试超时，Runtime 场景加载测试断言失败；当时静态库同时进行全量编译。
编译结束后不修改代码、不调整门槛，三项独立复跑全部通过（35.12 / 5.21 / 2.44 s）。
保留首轮失败，不描述为完整矩阵一次全绿。共享安装 SDK 消费者在首轮通过。

静态的缓存迁移、读取来源、容器、资产加载及版本账本 5/5 通过。
随后静态 C/C++ 接口、真实 GPU 像素和安装 SDK 消费者 21/21 通过；该轮开启 Vulkan 校验，
检查 CTest 原始输出无 Validation Error/VUID。共享首次结果及独立复跑、静态结果和像素日志的摘要见
[验证摘要](artifacts/0.44-granit-integration-checks.json)。

## 剩余限制

设备能力快照和工作线程变体选择尚未实现；完整 4K Cook、全场景驻留预算和 Linux 回归尚未验收。
对整个旧渲染实现运行 clang-tidy 仍有既有排版/初始化告警，不能将本次检查描述为全文件无告警；
本轮保持改动范围，不进行无关重构。
