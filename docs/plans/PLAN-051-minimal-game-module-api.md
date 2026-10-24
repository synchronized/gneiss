<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-051：收缩公共 SDK，保留游戏模块边界

## 状态

实施中。前两步已落地：模块头文件独立、调用原生对象，模板及 Lantern Gallery 已迁移。
Game Context 用例初始化改为原生对象；C11 头文件检查拒绝引入完整 SDK。
后续值类型与完整 C API 删除尚未完成；目前不能删除整个 include 目录。

## 目标与非目标

同源码构建的 Engine、Runtime、Editor 和工具统一消费 src 原生接口与共享值。
游戏动态模块保留生命周期、输入、日志及实际游戏行为所需的显式协议。
不通过把整套 SDK 改名搬进 src 宣称完成，不新增第二套对象状态。

## 已确认决策

见 [ADR-080](../decisions/ADR-080-minimal-game-module-api.md)。

## 实施顺序

1. 切断 Game Module 对完整 Application/World/Reflection API 的传递依赖；回调直接调用原生对象。
2. 提取模块协议的最小值类型闭包，迁移游戏模板及游戏示例。
3. 将内部共享值移入 src，消除重复结果和标识类型；转换只在真实协议边界存在。
4. 删除完整 C API 的生产适配层与安装声明；有价值的功能用例迁移至原生入口，删除纯 SDK 契约用例。
5. 收敛包配置、consumer 示例、ABI 检查和文档，编译所有保留目标。

## 验收与风险

公共 include 只包含游戏模块协议及必需类型；内部不依赖 Application 等公共 API。
原生结果、标识及值类型不维护内外两套定义。动态游戏模块仍可构建。
按用户要求只编译并做格式/边界检查，不执行测试、大场景和 CI。
这是接口收缩，原完整 SDK 消费者不再兼容；阶段完成不能描述成全部迁移完成。
