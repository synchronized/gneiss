<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：场景示例移除旧服务协议

日期：2026-11-11。

## 变更

temple、stable_runtime、lantern_gallery、property_inspector 直接使用 Scene 资产请求，
从本次请求捕获固定内容并组合、实例化，不再使用 scenes().open/activate 或 scene_load_completion。
examples/support/scene_activation.hpp 仅复用示例内的内存装配及视觉请求提交，返回实例化回执
和批次观察句柄；不增加生产服务、缓存或阻塞 I/O 包装。
启动阶段继续驱动 Application，等待视觉批次终态后进入演示；视觉失败打印资产 URI 和错误码。
修正 property_inspector 的布尔加载函数在失败时返回整数 2、被隐式视为成功的问题。

## 验证

Windows Clang Debug 构建通过；application_world_example、property_inspector_example 通过。
实际源码分层检查及 git diff --check 通过。examples 中已无 scenes() 调用。
未执行三个窗口示例、大场景、性能基准、跨平台或远端 CI。

## 剩余

[PLAN-113](../plans/PLAN-113-application-scene-boundaries.md) 仍需迁移旧测试调用与回执类型，
删除 scene_service/runtime/loading 并完成统一验收。
