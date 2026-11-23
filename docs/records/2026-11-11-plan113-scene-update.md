<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：拆出来源更新内存算法

日期：2026-11-11。完整计划尚未完成。

## 变更

scene_application.hpp/.cpp 改名为 scene_update.hpp/.cpp，同步 CMake、内部接口清单和所有引擎头文件引用，
不保留兼容头。从 scene_service::apply_source 拆出 rebase_scene_update，与 apply_scene 位于同一模块。
前者读取运行名称、组件、父关系和变换，调整调用方的临时组合结果，不修改 World、不加载资产、
不保存服务状态。来源未修改的变换保留运行值，来源修改时覆盖，行为与迁移前一致。
失败后调用方丢弃临时组合结果；应用失败时仍由调用方使来源基线失效。

## 验证

Windows Clang Debug 构建通过，10 项定向测试通过：editor.scene-loader、scene-load-application、
scene-load-preparation、scene_instance、scene_structural_reload、prefab-runtime-application、
scene-instantiation、prefab_structural_reload、editor.asset-scene-commands、editor.document-editing。
分层反例检查发现旧文件名匹配遗漏，更新 check_core_boundary 后完整反例检查通过；实际源码
分层检查、git diff --check 通过。未运行大场景、窗口 GPU、跨平台或远端 CI。

## 剩余

scene_service 仍协调来源内容加载、资源引用与异步请求，打开/激活/重载协议尚未删除。
后续继续迁移宿主调用链，并完成 render_extraction 与 application 的目录整理。
