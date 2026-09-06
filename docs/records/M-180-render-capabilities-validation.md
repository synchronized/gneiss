<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-180：诊断整合与版本验收

## 结果

渲染完成回执和退出诊断现同时报告 CPU 构造/排队信息、GPU 整帧时间、阴影/不透明/色调映射阶段
时间，以及环境来源、回退状态、强度和旋转。GPU 查询不支持或暂不可用时继续渲染，并保留明确的
能力与样本计数。

版本号已提升至 0.27.0，路线图和变更记录同步收口。

## 本地验证

- Windows Clang Shared Debug：完整构建及 132/132 测试通过，包括安装消费、Runtime、Editor、
  Lantern Gallery 和 Granit 平台冒烟。
- Windows Clang Static Debug：完整构建及 130/130 测试通过。首次完整测试仅停止协议在冷启动压力下
  超过原有 5 秒等待；测试现使用 15 秒总上限并在超时时附带标准输出和错误输出，完整复跑通过。
- Windows MSVC Shared Debug：启用 Editor、Runtime 与 Granit 平台后完整构建及 132/132 测试通过；
  同时修复两处仅由 MSVC 报告的变量遮蔽警告。
- C11/C++20 头文件、旧 ABI consumer、Material v1/v2 兼容、工程 v1/v2 兼容均通过。

## 尚未执行

- Linux Clang/GCC、Sanitizer 与远端 Granit Runtime Actions 需要推送并手动触发；当前任务没有新的
  远端操作授权，因此不把它们描述为已验证。
- Editor 视觉效果仍建议在合并前人工观察 PBR 与环境旋转；不阻塞代码侧版本收口。
