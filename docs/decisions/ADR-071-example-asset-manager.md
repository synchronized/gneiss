<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-071：以 Granit 示例资产管理模型替换 CAORS

- 状态：提议；移除 CAORS、采用 Granit 示例的方向已获用户提出，具体移植方式待审查。
- 日期：2026-10-22

## 背景

CAORS 已进入输入请求与 CPU 数据持有，但未替代 Gneiss 的来源复验、版本选择和应用事务。
用户要求先审查采用 Granit 示例 Asset Manager 的方式，再逐项分析并处理额外复杂度。
示例是私有源码而非安装 SDK，直接链接会带入具体任务系统及平台构建约束。

## 推荐决策

- 使用固定 Granit 提交的最小源码移植，保留其 MIT 许可证、原版权及来源记录。
  不依赖开发者相邻 checkout，不修改 Granit，不将移植版描述为正式上游 SDK。
- 复用现有 engine/resource/asset_manager，采用 Source 读字节、Loader 解析、
  强类型请求观察和 shared_ptr<const T> 结果；不创建第二个 manager。
- 注入 Gneiss 现有 VFS 和任务执行器，不带入第二个线程池；CPU 路径不要求图形平台开启。
- 类型分发不要求字段反射或统一资源基类。内部类型不暴露到公共 ABI、安装头和 ECS 组件。
- Runtime 负责采用和场景切换，Render 负责 GPU；历史及版本策略不进入通用加载核心。
- 先替换依赖并保留现有保证，再逐项审查复验、版本、原子提交与历史策略。
  不把“示例没有此功能”当作删除既有功能的理由。

## 影响

预期删除 CAORS 接线、DLL 分发和资源基类包装；实际收益须在实施后验证。
移植版差异、同步发布顺序、取消/关闭、失败重试与缓存回收由 Gneiss 维护。
本提案不改变公共同步 API、历史恢复结果或来源校验强度。

若接受并实施，本决策替代 ADR-068 的继续扩大 CAORS 接入方向，以及 ADR-069 中
CAORS 原生请求/资源基类的实现选择；其 CPU/GPU、Runtime/Editor 分层与失败保证继续有效。
当前源码仍使用 CAORS，不能提前更新 Concept 为已经完成的替换。

## 替代方案

- 直接链接示例 target：改源码少，但引入示例调度、平台与构建耦合，不推荐。
- 提取 Granit 正式公共组件：有长期复用价值，但需独立上游需求，不作为本轮前置。
- 保留 CAORS 并修复发布：有既有接入基础，但不符合本轮移除方向。
- 完全另写通用框架：容易重建同样复杂度，优先移植已有小模型并删除被替代路径。

范围、源码基线、八项分析与验收统一见[迁移计划](../plans/PLAN-051-asset-manager-replacement.md)。
