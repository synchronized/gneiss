<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景域所有权收敛验收

## 范围与结论

检查 scene_session、scene_load_service、application_scene_state 后，确认它们分别负责
活动/退役域、在途候选和 World/实例服务所有权，不直接合并。实施依据见
[模块布局计划](../plans/PLAN-051-module-layout.md)，当前对象入口见
[源码目录](../concepts/repository-layout.md)。

- 场景域只保留两个私有所有者，删除 VFS、Render/Prefab 加载器引用及独立 World RID。
- World 借用句柄从唯一 World owner 获取；实例服务通过借用指针访问。
- 初始化使用局部所有者，成功后提交；异常或无效实例服务不发布半成品。
- Session 同样在场景域初始化成功后发布 active 和借用依赖。
- 按成员逆序析构，先释放 Scene 及资源引用，再撤销 World RID。

公共接口、多个 Scene 共用 World、取消、候选激活及退役流程保持不变。
资源服务仍须比借用它们的场景实例活得更久；不因删除冗余引用改变这一契约。

## 本地验证

2026-10-21，Windows Clang：

- 相关 Engine、scene-load-builder、scene-load-application、editor.asset-scene-commands 构建通过。
- 11 项 CTest 通过，覆盖原生 Application、场景加载、Prefab 准备/刷新/终止、结构重载、
  场景构建、编辑命令和边界正负向检查。
- 场景构建测试增加未初始化查询、重复初始化保持原对象的检查；保留候选切换、取消清理
  与旧 World 句柄失效验证。
- 静态无线程配置的 CAORS Application 构建及测试通过。
- 修改源码的 clang-format 检查及 git diff --check 通过。

构建日志位于 build/scene-state-ownership-build.log 和 build/scene-state-ownership-static.log。
未注入内存分配失败，未运行 GUI 手动验收、Linux/Web 或完整发布矩阵。

## 剩余工作

Session 的资源重载编排与场景切换仍在同一对象，需继续审查职责；本次不宣称整个
架构迁移完成，也不为减少类数量取消必要的加载状态。
