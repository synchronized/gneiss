<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-069：资源模块与运行时组装边界

- 状态：已接受，分批实施
- 日期：2026-10-21

## 背景

engine/asset 与 function/asset 分散资源能力，统一管理器仍混合缓存、渲染配置、
上传通道与应用回调。局部所有权修正没有消除跨层协议，单纯移动目录不能解决问题。

## 决策

- 合并为 engine/resource，负责 CPU 资源描述、加载、缓存和引用；CAORS 是内部实现。
- function 保留 Input、Scene、World、Render 的运行时行为；GPU 生命周期归 Render。
- Application 的系统组装与场景切换流程归 src/runtime，apps 保留薄入口。
- 依赖 Application 的游戏上下文及 C 适配同属 Runtime；Engine 不反向包含 Runtime。
  混合提供资源和 Application 入口的渲染、场景适配文件暂整体归 runtime/api，复用
  engine/api 的值转换辅助；不为目录迁移修改公共符号，也不新增动态库边界。
- Editor 文档管理编辑状态，不复制运行时场景树。保留一个 World 多个 Scene。
- 资产管理器不执行场景或编辑回调；应用层在修改成功后提交已准备的资源版本选择。
- 不新增全局 locator，不并行实现第二套资源状态机，不改变公共 SDK 命名规则。
- 复用已有对象；最终名称与文件以实际职责确定，不为布局图增加包装类。

目标文件与对象布局、实施批次及验收统一见
[模块布局计划](../plans/PLAN-051-module-layout.md)。本决策细化 ADR-068 的资源归属，
替代其将资源管理长期放在 function/asset 的目录安排；保留其接入与平台契约。
ADR-064 的共享资产和一 World 多 Scene 决策保持有效。

## 影响

先移除反向调用，再逐步分离 CPU 描述与 GPU 资源，最后调整目录和构建边界。
迁移期间保留可验证的候选隔离、失败原子性和取消行为，不能宣称目标已经全部实现。

## 替代方案

- 只搬文件：不能修复反向依赖与多个缓存协议。
- 使用全局 locator：缩短获取路径，但隐藏依赖，无法解决职责混合。
- 一次重写全部资源系统：回归范围过大，难以保留生命周期语义；采用分批替换并删除旧路径。
