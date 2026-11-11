<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：Runtime 来源重载归属宿主

日期：2026-11-11。

## 变更

runtime_scene_loader 持有来源重载请求和完成回执；Runtime 不再调用 scenes().reload。
资产完成通知只设置就绪标记，宿主更新阶段捕获本次请求的固定内容，组合前后来源，调用
rebase_scene_update/apply_scene。视觉加载使用现有批次，回执领取不推进工作。
保留空基线、跨 World、失同步和已卸载场景检查；结构更新失败后不冒充已同步。
未创建新的通用服务；迁移期回执与旧服务仍待剩余消费者迁移后删除。

## 验证

Windows Clang Debug 构建、源码分层检查和 git diff --check 通过。
5 项回归通过：runtime.scene-loader、runtime.smoke、editor.runtime-scene-loading、
scene_structural_reload、prefab_structural_reload。
新增宿主入口测试覆盖 Scene/Prefab 名称更新与实体身份保留、重复提交、缺失文件保留基线、
无效基线及卸载后拒绝重载。Prefab 名称测试使用未被场景覆盖的子对象，保留实例根命名语义。
未运行大场景、窗口 GPU、跨平台或远端 CI。

## 剩余

[PLAN-113](../plans/PLAN-113-application-scene-boundaries.md) 尚需迁移测试和示例的旧调用，
整理回执类型并删除 scene_service/runtime/loading，最后统一验收。
