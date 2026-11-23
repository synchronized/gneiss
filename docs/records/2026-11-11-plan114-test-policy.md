<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-114：测试规则与首轮清理

日期：2026-11-11。

## 变更

建立[测试指南](../guides/testing.md)与[有限整理计划](../plans/PLAN-114-test-maintenance.md)，
AGENTS 和文档索引链接指南。计划列出旧场景测试各组的处置方向，未声称已完成逐条去重。

场景基准报告增加实际 cancel_requested 标记；校验允许零耗时但拒绝未取消、未释放观察者、
负数和非有限取消时间。两个性能脚本的同步实例化预算改为显式选项并写入报告，
默认仍记录耗时，不沿用旧候选交换 5 ms 门槛；其他性能与功能检查保持。

异步关闭测试从 scene_load_application_test 移入 native_application_test，使用真实场景资产
请求和资源批次。覆盖线程池/协作模式的空闲、提交资产、提交批次关闭，重复关闭幂等、
关闭后拒绝新请求、批次失效、资产请求退出等待、任务回收与关闭回调一次。
新用例不承诺 I/O 一定处于某个内部阶段；删除旧阶段耦合。
已核查的 native-application 标记 cpu/integration；其余标签仍待审核。

## 验证

Windows Clang Debug 的场景基准、原场景加载测试和 native_application 目标编译通过。
native-application 定向 CPU 回归通过。脚本语法、零耗时、未执行取消、未释放、
可选预算、功能模式不执行性能预算，以及两个 CLI 拒绝零/负数/NaN/无穷预算检查通过。
git diff --check 通过。未运行旧场景整套回归、GPU、大场景或远端 CI。

## 剩余

PLAN-114 未完成：旧场景测试其余分组、辅助代码去重、全套测试标签审核仍待推进。
PLAN-113 的旧服务尚未删除。
