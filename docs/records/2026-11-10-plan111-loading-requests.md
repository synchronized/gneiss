<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-111：加载请求接口收敛验收

2026-11-10，Windows Clang Debug，本地验证。

## 变更

- Application 主循环统一推进；advance_loading 改为私有，不再有外部加载专用推进调用。
- 场景打开、激活和原位重载使用 scenes() 及移动请求对象；多资源编辑使用 load_assets 返回的批次请求。
- 删除 Application 的 request/poll/prepare/discard 旧包装，迁移 Runtime、Editor、示例及测试。
- 请求观察原有操作状态，不保活 Application、不新增缓存；移动转移观察权，放弃请求释放对应槽位。
- IPC 会话和修订留在 Host；查询和领取不驱动加载。完整契约见 [Application 参考](../reference/application.md#加载请求)。

## 验证

- windows-clang-debug 全部目标编译通过，包含迁移后的示例及 GPU 测试目标。
- 16 项定向 CPU 回归全部通过：native-application、scene-load-application、texture-load-application、
  scene_structural_reload、prefab_structural_reload、prefab-runtime-application、application-log-fatal；
  editor 的 asset-scene-commands、prefab-component、prefab-author-operations、scene-document-edit、
  scene-document-snapshot、scene-document-versions、prefab-edit、asset-edit、scene-loader。
- 请求测试覆盖移动、放弃后重新提交、跨线程拒绝、跨宿主激活拒绝、取消、关闭和宿主析构后的失效查询。
- check_core_boundary.cmake、test_core_boundary.cmake 及 git diff --check 通过。
- 源码、示例和测试中无外部 advance_loading 或已删除 Application 轮询接口调用。

首轮失败中，生命周期用例缺少资产挂载，已补齐测试配置；编辑器取消用例仍要求共享缓存回滚，
已改为验证迟到结果不修改文档或历史。Application 每帧提交的共享资产工作可以继续，取消编辑不承诺撤销缓存。

## 范围限制

未运行 GPU、大场景、跨平台或远端 CI；未推送和发布。本记录完成 [PLAN-111](../plans/PLAN-111-loading-request-api.md)
规定的本地验收，不代表完整版本发布验收。
