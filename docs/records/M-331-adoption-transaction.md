<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：版本采用的使用者提交边界

2026-10-15，继续落实 [ADR-064](../decisions/ADR-064-shared-assets-and-scene-instances.md)
的版本采用规则。本轮只完成缓存原语，未接入宿主场景切换。

## 实现

resource_cache 的 adopt_versions 增加使用者提交回调重载。先复制并验证候选选择映射，
完成全部分配后调用回调；回调成功才无分配交换选择映射。失败或异常不改变选择与修订号。
旧入口委托该流程，不再维护独立实现。映射未变化时仍执行回调，支持新实例接受已采用版本，
但不增加缓存修订号。

采用同步闭包范围而非可长时间持有的准备令牌，避免在准备与提交之间推进其他缓存任务。
这是内部所属线程接口：回调不得修改缓存或递归采用，也不能异步保留；回调自己负责
使用者状态的失败原子性。缓存不能撤销回调已经写出的业务状态。
后续宿主接入应让已有场景批量替换作为该回调，不能在回调中先产生不可回滚的修改再执行
可能失败的操作。回调结束后不再分配候选映射。

## 验证

Windows Clang Debug 引擎及 resource_service 测试构建通过，定向 CTest 3/3 通过：
resource_service、core-boundary、core-boundary-negative。补充幂等提交测试后再次运行
resource_service 通过。

测试覆盖提交返回失败、异常、空回调、候选无效不调用回调、回调期间仍观察旧选择、
成功后发布及相同映射仍执行回调。resource_cache.cpp 执行 clang-tidy，未报告自有源码告警。
未执行 GPU、完整 Sponza、跨平台、安装与远端矩阵。

## 后续

统一渲染准备配置的选择域编码，再将宿主场景切换放入这一提交边界。
此后才能接入就绪租约查询及迁移 Editor 同步消费者；Application 级共享缓存仍未完成。
实施顺序见 [开发计划](../plans/DEV-051-architecture-boundaries.md#同步入口收敛顺序)。
