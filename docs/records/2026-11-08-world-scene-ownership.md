<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# World 场景集合所有权迁移记录

## 范围与结果

推进 [PLAN-105](../plans/PLAN-105-game-object-runtime-model.md) 的运行存储部分。
World 独占 scene_instances，Runtime scene_domain 不再分别拥有 World 和场景集合。
原场景拥有者改为私有 scene_instance_state；scene_instance 提供借用、查找和卸载接口。
卸载增加所属线程校验；World 析构先回收场景成员，再销毁 ECS。

接口契约与生命周期限制见 [World](../reference/world.md)。

## 验证

- Windows Clang Debug 默认构建通过。
- 84 项相关 Editor、Scene、Prefab、World、组件、渲染采集与边界测试全部通过。
- 新增重复初始化、跨 World、跨线程卸载、场景槽位复用、复制借用句柄失效、
  对象随场景卸载失效及 World 析构释放组件检查。
- 头文件边界测试改为禁止访问 scene_instance_state 私有实现。
- git diff --check 通过；未运行 GPU 大场景或远端 CI。

## 剩余工作

本批只完成场景集合拥有权及借用入口，成员记录仍由集合内部状态维护。
跨场景重挂接限制、文档地址到实体的独立映射、Prefab 通用增量投影、
运行来源记录删除与旧 node_* 接口清理尚未完成；不代表三项任务或 PLAN-105 全部完成。
