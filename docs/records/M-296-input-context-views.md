<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-296：输入与 Game Context 强类型借用

日期：2026-10-06。

## 缺口与改动

输入自由函数要求裸 Application 和整数动作，Game Context 只返回整数 World 与实体。
新增非拥有 `action_id`，在 Application 提供六个输入成员入口；Game Context 增加
`world_ref`、`entity_id`、`action_id` 重载。旧整数别名和自由函数保留，不改变 C ABI、映射状态或回调所有权。

强类型查询在失败时保留已有输出；动作重载成功使旧 generation 失效，失败保持旧映射。
所属线程及父对象失效规则沿用 C 契约，Game Context 的跨线程错误仍为 invalid_handle，不改写成 Application 的 invalid_state。
当前契约见 [输入](../reference/input.md#c-入口) 与 [Game Module](../reference/game-module.md#c-借用视图)。

## 验证

- Windows Clang Debug 共享、静态全量构建通过，包含公共头独立编译。
- 共享 SDK、输入、上下文、安装稳定运行时专项 21/21 通过，13.59 秒。
- 静态同组及 Runtime 面板 22/22 通过，19.13 秒。
- 输入测试覆盖零动作、值快照、空事件队列、缺失动作/映射、失败输出保留、跨 Application、跨线程、
  重载使旧动作失效、错误结构尺寸、销毁父对象，以及原整数入口与强类型入口一致。
- Game Context 测试增加真实上下文中的强类型 World、根实体及动作查询，验证错误线程及上下文销毁后输出不变。
- 安装前缀的独立 C/C++/反射消费者验证新成员入口；两种链接方式均 3/3 通过。
- clang-tidy 新输入测试仅报告既有 Application 初始化宏的有符号位运算告警；没有降低检查等级。
- 本组未重跑完整 CTest 或远端矩阵，不作为发布验收。

清单新增 10 项人工语义映射，累计 49/102 reviewed；类型、回调和剩余场景接口仍需继续审查。
