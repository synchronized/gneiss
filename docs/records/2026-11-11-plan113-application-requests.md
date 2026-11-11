<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：Application 请求边界回归迁移

日期：2026-11-11。

## 变更

native_application_test 不再包含旧 scene_service，也不使用场景候选和激活接口。
请求生命周期改用 Application 资产批次：保留空请求、跨线程拒绝、移动、放弃、
关闭后失效检查，增加取消结果领取和两个 Application 的批次隔离检查。
旧跨所有者激活断言随旧协议移除；宿主场景归属由 Runtime 来源重载回归覆盖。
本次仅修改测试与计划，未修改生产实现。

## 验证

Windows Clang Debug 的 gneiss_native_application_test 编译通过，
gneiss.native-application 定向 CPU 回归通过；git diff --check 通过。
未运行窗口 GPU、大场景、完整测试矩阵或远端 CI。

## 剩余

[PLAN-113](../plans/PLAN-113-application-scene-boundaries.md) 仍需迁移四个旧消费者：
scene_load_application、scene_load_baseline、main_loop_response、asset_pixel。
之后才能删除旧服务与 Application 中的装配；本次不表示计划完成。
