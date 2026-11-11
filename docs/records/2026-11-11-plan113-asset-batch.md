<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113 第二组：资产批次归属和完成通知

日期：2026-11-11。对应 [PLAN-113](../plans/PLAN-113-application-scene-boundaries.md) 第二组。

## 结果

- asset_batch_request、asset_batch_types 与内部 resource_load_group 迁入 function/asset，
  使用 gneiss::engine；不保留 runtime 兼容别名。
- 删除 submit_assets/prepare_assets 两套准备入口，统一 begin；缓存校验、查询、提交只维护一处。
- 删除批次 session/revision 和固定 256 项限制；调用方的文档及 IPC 版本语义不变。
- 批次继续串行组合既有资产请求，不新增缓存。完成订阅只写独立就绪回执，
  在宿主推进时消费；在途请求不再逐帧查询状态，取消后旧通知失效。
- 成功终态改为 completed，不暗示编辑提交或 GPU 完成；保留逐项结果和部分成功资源。
- 编辑器、场景视觉请求、测试和内部头清单同步迁移。
- 应用装配及迁移中的场景协调仅获准包含批次汇总头，仍禁止包含其他资产私有实现。

## 验证与限制

Windows Clang Debug 全量构建、分层检查、git diff --check 通过。
定向回归最终全部通过：assets.application、runtime.asset-reloader、render_asset_loader、
texture-load-service、texture-load-application、internal-header-visibility。

资产重载测试首次超时：旧测试期待回调异常转换失败回执，而现有异常策略要求向外传播。
测试改为捕获并确认传播，再验证原待完成请求可继续消费，重编译后通过。
未运行大场景、完整 GPU、跨平台和远端 CI；未改变 scene_service 的 World 所有权。
