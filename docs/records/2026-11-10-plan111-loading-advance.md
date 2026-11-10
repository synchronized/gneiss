<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-111 首阶段：加载推进与查询分离

2026-11-10，Windows Clang Debug，本地验证。

## 变更

- Application::advance_loading 统一推进文件回调、资产服务、通用批次、场景及视觉资源。
- run 每帧调用统一入口；场景、资产、准备批次和重载回执查询均不再推进工作。
- 替换 advance_scene_load 调用，迁移自定义宿主/测试驱动，编辑器推进错误进入原有失败路径。
- 增加查询不退休旧 World、主循环独立推进批次、跨线程及关闭后拒绝推进的验证。

## 结果

- windows-clang-debug 全量构建和修改后增量构建通过。
- check_core_boundary.cmake、git diff --check 通过。
- 16 项 CPU 回归通过：native-application、scene-load-application、texture-load-application、
  scene_structural_reload、prefab_structural_reload、prefab-runtime-application、application-log-fatal；
  editor 的 asset-scene-commands、prefab-component、prefab-author-operations、scene-document-edit、
  scene-document-snapshot、scene-document-versions、prefab-edit、asset-edit、scene-loader。
- 首轮发现重载测试仍依赖 poll 推进，迁移后通过。
- native-application 仍保留 PLAN-109 前的异常转错误码及日志回调吞异常断言；现验证异常传播，
  正常日志交付与关闭，日志异常终止由已有 application-log-fatal 子进程测试覆盖。

## 限制

未运行 GPU、大场景、跨平台或远端 CI。请求对象和 open/activate API 尚待后续迁移，
不得将本次结果视为 [PLAN-111](../plans/PLAN-111-loading-request-api.md) 全部完成。
