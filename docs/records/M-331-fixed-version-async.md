<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：异步资产固定版本模式

2026-10-15，继续 [共享资产方案](../plans/asset-scene-ownership.md)。

## 已实现

texture_load_service 的 submit_assets 可显式选择 fixed_version，默认行为不变。
固定版本请求始终准备并复验来源，不按 URI 提前返回已有缓存；暂存阶段交给加载器按版本
复用。已复用候选不加入上传数据，因此失败或取消时不会进入后端撤回列表。
进度将复用数量计入已完成资产数，上传预算仅计入实际候选。

成功完成结果的 versions 与 assets 一一对应，保留来源身份及版本，后续场景构建可以消费
确切租约。版本元数据在发布前准备好；失败清空，不返回部分版本。全复用批次不调用上传。

## 验证

Windows Clang Debug 构建通过。texture-load-service、scene-load-application、resource_service、
render_asset_preparation、render_asset_loader、core-boundary、core-boundary-negative 合计
7/7 通过；格式与 git diff --check 通过。
新增测试覆盖协作模式与线程池模式：首次上传、重复加载零新增上传、纹理复用时只上传新
材质、在途取消只撤回新材质、上传失败后重试，以及完成结果的版本和租约一致。
使用模拟上传后端，未验证真实 GPU 或完整 Sponza。

clang-tidy 已执行，仍报告提交/推进函数复杂度及其他既有风格告警；未关闭检查。
新修改上传初始化使用具名字段。未运行跨平台和安装消费者矩阵，未进行远端操作。

## 尚未接入

scene_load_service 尚未选择固定版本模式。场景构建仍通过 URI 获取资源，必须先改为消费
确切版本租约，不能增加一个隐式“最新版本”映射绕过该边界。
Application 级共享所有权、跨场景复用和共享任务取消隔离仍未完成。
