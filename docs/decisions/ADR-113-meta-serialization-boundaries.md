<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-113：Meta 借用访问与序列化策略边界

## 状态

2026-11-09：接受目标方向，部分已实施，完整收敛尚未完成。详见
[PLAN-108](../plans/PLAN-108-meta-serialization-api.md)。
延续 ADR-109 的结构化值和 ADR-112 的资产引用语义；不修改历史已完成记录。

## 背景

现有 Meta 已具备类型视图、分类 Ops 和容器视图，序列化已有通用遍历、JSON 与引用解析。
需要收敛的是属性访问门面以及内容、引用、校验、编辑元数据混合的边界，不是另建反射系统。

## 决策

Meta 以 type/property 句柄与 instance/const_instance 借用视图为公共门面；
使用 view/member/get/assign 与显式注册 commit。保留唯一类别 variant Ops，支持公开 traits 适配。
Meta 仅提供扩展元数据存储，不依赖 Serialization、Editor、资产类型或其服务。

Serialization 以格式无关 reader/writer 和通用遍历访问实例，JSON 为当前后端。
内容 codec、引用策略、字段策略独立定义并关联同一注册表；字段显式策略优先于类型默认。
读写上下文配置与操作临时状态分离；通用错误约束和领域业务校验分工明确。

value 保留结构化数据及对象引用，复制对象引用共享对象；历史快照必须显式捕获。
快照继续归 Serialization，不能把带策略的数据转换下沉到 Meta。
普通序列化直接遍历实例，不强制经过 value，也不自动保存对象图别名。

外部引用由调用层提供 resolver，序列化不发起或等待 I/O；资产调度沿用收集/绑定与
content/reference 协议。动态内嵌类型显式携带 GUID，引用环返回诊断。

## 句柄整理补充

2026-11-09：同意将 type/property 收敛为保活注册表的稳定记录句柄，精简 instance 的重复类型与
读写成员，并增加属性局部 metadata builder；对象动态类型门面返回反射类型的方向已同意。
这些新增接口尚未实施，不能据此描述当前能力。
动态查询失败形式、冻结后的结构类型记录归一化和跨表比较仍需按 PLAN-108 第二节确认。
确认前保留现有 result 查询与冻结行为；本补充不改变 Meta/Serialization 的依赖方向。

## 取舍

采用新 API 会触及大量调用方，尤其 instance 原只读名称需要整体迁移；不保留语义冲突别名。
句柄和范围保活注册表，不能解决被借用对象或容器元素失效，需要明确生命周期契约。
保留现有 Ops 与快照实现降低重复抽象；不采用一排可选能力指针、万能 value 或第二注册表。
Binary 后端和插件卸载不提前实现，以真实消费者决定后续扩展。

## 后果与验证

具体目标 API、阶段、限制与验收矩阵只在 PLAN-108 维护。
实现完成后更新 Reference 与验收 Record；本决策不宣称规划 API 已存在。
