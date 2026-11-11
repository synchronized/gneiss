<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-116：加载请求与执行推进分离

## 状态

2026-11-10：已接受，按 [PLAN-111](../plans/PLAN-111-loading-request-api.md) 实施。

## 背景

Application 的 poll_scene、poll_assets 和 poll_prepared_assets 同时驱动底层工作并领取回执，
导致调用者查询次数影响推进和旧 World 回收；普通请求还暴露 Host 的 session/revision。

## 决策

主循环拥有加载推进时机，查询与领取仅观察既有结果。场景请求对象收敛状态、取消与错误，
激活仍显式执行。普通资产复用 asset_request；编辑器批次仅承担批量编辑语义。
Host 保留协议关联信息。请求句柄不延长 Application 生命周期，关闭时安全失效。

## 影响与替代方案

调用方无需逐类 poll 才能使工作前进，查询行为可预测；自定义宿主需要显式驱动。
自定义宿主只驱动完整 Application 帧，不另提供加载专用推进入口。
保留轮询驱动虽改动小，但无法消除隐藏副作用。全部改为回调会增加取消与捕获生命周期负担，
因此先使用请求对象；不引入通用 future 或协程框架。

## 后续决策

2026-11-11：[ADR-117](ADR-117-application-scene-boundaries.md) 局部取代候选 World、
显式激活与退休 World 的共享协议设计；实现待 PLAN-113 迁移。其余异步查询和取消规则保留。
