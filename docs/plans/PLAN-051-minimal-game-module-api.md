<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-051：收缩公共 SDK，保留游戏模块边界


后续决定：动态 Game Module 也已撤销，当前状态见
[撤销模块计划](PLAN-051-remove-game-module.md)。本页保留先前迁移范围，不代表仍提供模块协议。

## 状态

本计划的代码迁移、安装边界及编译收尾已完成；最终行为测试按用户要求另行执行。

- 完整 Application、World、Scene、Render、Input、Reflection C API、公共 C++ SDK、
  对象兼容桥和旧值转换已删除，仓内宿主、示例及保留功能用例使用原生接口。
- 公共头只保留游戏模块协议及必需类型；导出清单包含 9 个实验性模块入口。
  C 结果、变换、日志与借用标识的转换只留在真实协议边界。
- Application 回调和 run/shutdown 不再带旧 context 参数；World 借用标识使用
  engine::rid，内部实体存储使用 uint64_t，原生 World 操作使用强类型实体/节点 ID。
- Game Context 弱引用原生 Application，操作期间持有临时强引用，并检查父对象关闭状态。
  World 唯一拥有者负责销毁，私有注册表负责借用 RID 的线程、类型和失效校验。
- 反射使用 engine::type_guid；运行期 engine::type_id 由 Engine 的单一名称注册表分配，
  cv/ref 归一化，零保留给 void；静态定制点位于 core/traits。见 ADR-081。
- 版本查询位于内部 Core，生成版本头不安装；共享模块包不要求消费者查找 Granit，
  静态引擎链接继续解析后端依赖。旧完整 SDK consumer 已撤销，模块模板编译检查保留。
- 原生流程与生命周期断言保留，纯 SDK 结构版本/包装契约用例删除；本阶段只编译，不宣称行为验证通过。

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

## 验证结果与延期项

Windows Clang Debug 全部保留目标编译通过；干净前缀只安装 6 个协议头，
仅指定该前缀的游戏模块模板配置与 DLL 编译通过，未安装 Granit 开发包。
格式、差异空白、核心边界、公共头闭包与 9 个模块符号清单静态检查通过。
构建日志位于 build/asset-migration/module-package-final.log，安装消费者日志为
module-only-configure.log 与 module-only-build.log（同目录，本地产物不提交）。

延期项：

- 最终行为验证独立延期：运行期身份、资源重载、场景、编辑器、跨线程和模块生命周期。
- 静态库、其他平台与 CI 矩阵未在本轮运行，统一验收时补齐。

## 验收与风险

公共 include 只包含游戏模块协议及必需类型；内部不依赖 Application 等公共 API。
原生结果、标识及值类型不维护内外两套定义。动态游戏模块仍可构建。
按用户要求持续完成全部迁移，只编译并做格式/边界检查，不执行最终测试、大场景和 CI。
运行期类型 ID 注册表已实现并接入资源键，区别于先前的 traits 移植；见 ADR-081。
最后保留独立的最终验证待办，包括运行期身份、资源重载、场景和编辑器用例的实际执行。
这是接口收缩，原完整 SDK 消费者不再兼容；编译完成不代表最终行为验收通过。
