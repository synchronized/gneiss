<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-103：执行器唤醒职责收敛

## 状态

已完成，2026-11-07；归属 [DEV-052](DEV-052-architecture-cleanup.md)。决策见 [ADR-108](../decisions/ADR-108-executor-wakeup.md)。

## 目标与边界

保留任务和渲染完成队列；结果先发布，再唤醒宿主。唤醒不执行业务回调、不保存结果。
删除资产操作、场景阶段变化和逐任务描述携带的通知。Application 负责装配。
不增加事件总线或第二套主线程回调队列，不修改文件后端，不引入永久阻塞或待处理请求忙轮询。

## 实施顺序

1. progress_notification 改名 wakeup_signal；任务 scope 持有信号，完成时通知对应宿主。
2. Application 创建带信号的资产任务 scope，成功配置后由 asset_manager 负责关闭；失败由 Application 关闭。
3. 移除 load_operation、scene_service 的通知。Render 保留执行器完成唤醒。
4. 更新内部头清单、测试和文档，编译并执行针对性回归。

## 验收

- 成功、失败、取消均先记录终态再通知；不同 scope 互不唤醒。
- 等待前已到达的通知不丢失；没有通知时允许休眠。
- 宿主引用释放后信号由 scope 保活，scope 关闭后释放，迟到任务不会访问宿主对象。
- 保留 Native 最长 4ms 的单次等待参数；浏览器不阻塞等待。
- 资产完成一次性、关闭、渲染完成和内部头边界测试通过；不运行大场景和远端 CI。

## 验收结果

Windows Clang Debug 完整构建通过；14 项针对性测试全部通过，包括唤醒竞态、scope 隔离、
迟到完成生命周期、scope 接管、任务调度、渲染执行器、资产与场景加载、Core 边界和头文件可见性。
未运行大场景、浏览器构建和远端 CI。文件 I/O 仍由宿主推进，未增加文件后端唤醒协议。
