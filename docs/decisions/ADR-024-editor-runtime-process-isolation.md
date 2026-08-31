<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-024：Editor 与 Runtime 宿主进程隔离

- 状态：已接受
- 日期：2026-08-31

## 背景

Editor 需要运行当前工程，验证启动场景和游戏交互。若在 Editor 的 Application、World 或场景实例
中直接切换到运行模式，作者状态、撤销栈、后端全局状态和运行时临时修改容易共享所有权；运行时
崩溃也会同时终止 Editor。另一种方案是在同一进程创建第二套 Application，但仍需处理平台后端、
崩溃边界和停止超时。

## 决策

- `gneiss_runtime` 作为 `apps/runtime/` 下的独立薄宿主运行工程，不依赖 Editor 或 ImGui。
- Editor 首版通过独立进程启动 Runtime 宿主，而不是在作者 Application 中切换运行模式。
- Editor 与 Runtime 宿主共享无 UI 的工程描述解析和 Engine 能力，不共享 World、Scene Instance、资源
  句柄、撤销栈或后端原生对象。
- Runtime 宿主默认读取工程的 `startup_scene`。Editor 启动前要求用户处理未保存修改，不创建私有临时
  场景格式。
- 首版进程边界只传递工程根、启动选项、退出请求和可观察状态；不得传递 Editor 内部对象地址或
  Granit 原生句柄。
- Runtime 宿主和进程控制协议保持 Experimental，不进入 Stable C ABI。

## 影响

- 运行时崩溃、资源泄漏和全局后端状态与 Editor 隔离，运行结果更接近最终游戏宿主。
- Editor 可以在 Runtime 宿主退出后保持作者会话、脏状态和撤销历史不变。
- Windows/Linux 需要各自实现进程创建、状态查询和停止，但行为契约必须一致。
- 未来需要无缝嵌入式预览时，可以另行设计渲染表面共享或进程间协议，不改变首版 Runtime 宿主所有权。
- 自动测试可以直接运行 `gneiss_runtime --project <root> --smoke`，无需驱动 Editor UI。

## 替代方案

- 在 Editor 当前 Application 中切换运行模式：实现表面简单，但作者状态和运行时状态难以可靠隔离。
- 同进程创建第二套 Application：能够隔离部分 World 状态，但不能隔离崩溃和平台后端全局状态。
- 一开始建立完整 IPC 与嵌入式游戏视图：范围过大，在基础启动、停止和诊断闭环验证前缺少依据。
