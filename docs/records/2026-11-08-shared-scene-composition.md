<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 共享场景组合入口

[PLAN-105](../plans/PLAN-105-game-object-runtime-model.md) 的迁移顺序已调整为共享纯组合、
统一有效对象应用、删除旧运行来源状态；边界记录于 ADR-110。

Engine 提供 compose_scene 和共享 object_address。编辑器从不可变文档记录恢复描述与固定来源，
随后调用该纯函数；Runtime 首次场景实例化使用同一函数生成覆盖后的有效对象。
删除编辑器独立的组合算法及其地址/结果类型，保留输入恢复适配。
组合不访问文件、World 或 GPU，保留覆盖冲突；普通对象及多个同源实例使用明确地址和父地址。

Windows Clang Debug 默认构建通过。84 项 CPU 定向回归初次通过 83 项；
剩余失败为实例化非法来源输入的错误码变化。入口恢复 invalid_argument 校验后，
重新构建并重跑场景实例化、Prefab 实例、来源刷新及文档快照 4 项，全部通过。
新增纯组合测试覆盖缺失/空来源以及 Runtime 共用入口与编辑器结果的一致性。
未运行 GPU 大场景、远端 CI 或其他平台矩阵。

本次未删除 prefab_binding：运行创建和旧来源重载仍依赖它。
准备阶段依赖收集、独立 Prefab 路径、通用应用以及持久父地址仍待贯通；不将本阶段记为计划完成。
