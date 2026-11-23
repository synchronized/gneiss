<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Render 与 Prefab 错误契约审查

## 范围

[PLAN-118](../plans/PLAN-118-error-contracts.md) P3 定向审计。
当前规则见 [Render](../reference/render.md) 和[序列化](../reference/serialization.md)。

## 回执与关闭证明

- 执行器工作线程从提交任务复制 sequence，命令完成后先释放捕获数据，再在锁内发布回执。
  executing 清零和 idle 通知发生在回执入队之后；flush 持同一锁等待队列及在途任务排空。
- Service 要求所属线程调用，执行器私有；初始化创建新执行器，只提交初始化命令。
- shutdown 先调用 finish_frames：即使读取到历史错误，也持续消费旧回执，之后才提交关闭命令。
- 因此成功提交并 flush 后，缺失或序号失配不能是正常的异步未完成，改为全配置断言。
  初始化删除回调写共享局部结果的重复状态，直接读取回执状态。
- 不改变队列拒绝、可恢复任务重排、后端失败和优先报告历史错误的关闭规则。

## 保留与删除的诊断

| 来源 | 处理 |
| --- | --- |
| 执行器/资源存储的显式预算拒绝 | 保留 out_of_memory，调用方可降低请求预算 |
| 队列积压、可恢复命令中间状态 | 保留 not_ready 和重试，不能断言 |
| Granit 返回及读回布局不符合要求 | 保留后端错误，第三方结果不是已证明的本地不变量 |
| Prefab prepare/modify_component/serialization::apply_value | 会调用注册适配器，保留显式操作失败 |
| Prefab fatal 函数 | 更名 abort_replay，明确只停止本次重放，不终止进程 |
| replay_value_edits | 检查 apply_value_edits/validate_persistent_value 全链，无外部回调或 internal/OOM 产生点，删除不可达分类分支 |

serialization::apply_value 的对象构造空结果仍沿现有协议返回 out_of_memory；
本批不将可扩展工厂的失败机械转换为进程终止。真正分配异常保持原传播边界。

## 验证

Windows Clang Debug 和 MSVC Release 三个目标构建通过，各五项测试通过：
render_executor、value_edit、prefab_property_override、fatal-render-frame、fatal-render-command。
git diff --check 通过。扩展已有执行器生命周期用例，以 internal/out_of_memory 作为终态，
检查资源释放先于回执、预算归还、失败后重试及新回执身份。没有为每个断言建立新的死亡测试。

## 限制

没有运行实际 GPU 初始化、设备丢失、窗口关闭或跨平台/静态库矩阵。
CPU 用例验证执行器时序与错误传播，不替代真实后端端到端测试；P4 最终验收仍未完成。
