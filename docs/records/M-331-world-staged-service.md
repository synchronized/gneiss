<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：World 候选对象服务入口

World 私有服务增加发布组创建、候选对象创建、候选叶对象销毁和发布入口；仍使用唯一 World
注册表校验线程与句柄 generation。候选对象成对创建实体和节点，变换或组件准备失败时清理
本次对象并清空输出。删除先验证发布组、实体与节点对应关系及叶节点约束，再处理对象，
不先删除实体再尝试节点删除。候选相机不自动改变当前活动相机。

Windows Clang Debug 构建通过，world-service-boundary、core-boundary、core-boundary-negative
测试 3/3 通过。新增覆盖不可见计数、非叶拒绝、实体节点错配、失败创建不留对象、重复删除、
错误线程发布、重复发布和发布后的正常销毁。定向 clang-tidy 未报告新增入口告警；旧入口仍有
参数类型相似和尾逗号告警。

本轮是接入基础，尚未把 Prefab 控制器切换到该入口；不代表 Editor 生产流程已可分帧完成。
未验证真实窗口、GPU、完整场景性能或跨平台构建。
