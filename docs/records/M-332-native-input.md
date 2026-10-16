<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-332：Input 动作映射原生化

2026-10-16，按 [DEV-051](../plans/DEV-051-architecture-boundaries.md) 从日志继续推进 Input。

## 已完成

动作映射使用原生 action_id、action_state 与强类型 result，状态布尔字段不再复用 C 结构。
Application 将原生动作对象传给 Input；动作的 C 句柄及状态布局转换集中于 input_api。
保留零句柄、代次失效、跨服务拒绝及失败输出语义，公共 ABI 和 SDK 用法未改变。

原生替换入口原先允许手工构造的非法键码绕过 JSON 校验，随后键盘位数组访问可能越界。
现在原生替换与 JSON 解析均检查动作名称、重复名称、绑定数量、键码和有限非零缩放值。
失败不替换旧映射、不推进代次。纯映射校验独立于 JSON/VFS，原生单元测试无需依赖解析器。

## 验证与剩余

本地 Windows Clang Debug 构建 Engine、Editor、Runtime 与输入测试通过。
9 项输入、动作映射、SDK、Game Context、宿主启动与依赖边界回归通过。
增加非法原生映射保留旧句柄、NaN、重复名称、零句柄、跨服务、换图代次失效、
按帧清理瞬时状态和失焦释放动作测试。

键鼠事件、键盘/指针快照、Granit 平台输入适配仍使用 C 类型，Input 整体迁移尚未完成。
Application 的动作方法返回值仍是兼容结果码，留待 Application 收敛；未声称整个模块已原生化。
未执行 Linux、静态安装矩阵或人工跨屏输入测试。
