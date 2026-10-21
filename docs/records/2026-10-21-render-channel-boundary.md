<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 渲染上传通道与资产管理器分离

2026-10-21，0.51 特性分支；[模块布局计划](../plans/PLAN-051-module-layout.md)批次 D 的独立子步骤。
此步骤不依赖完整缓存合并，因此先行实施；批次 B/C 没有据此标记完成。

## 实际改动

- 删除 asset_manager::create_channel、set_profile、query_ready，以及接收渲染完成回执的
  prepare_adoption 重载。资产管理器不再包含 texture_load_service 头或上传后端类型。
- Application 的活动通道、场景候选和来源快照三个创建点直接装配已有 texture_load_service。
  继续注入同一份 CPU 文档缓存，没有创建第二套缓存或通道协调对象。
- 设备准备配置与就绪租约查询直接使用既有 render_asset_loader。
- 渲染租约所属服务校验与版本选择准备归 render_asset_loader；Application 保留完成结果校验，
  修改成功后才提交版本，失败释放准备状态。
- 新增资产管理器不得包含上传通道、后端和完成回执类型的正反向边界验收。

本批保留原所有权与关闭顺序。asset_manager 仍拥有 render_asset_loader、旧资源缓存，
并保留阻塞 acquire/reload；它还不是最终纯 CPU 资源管理器，不能提前搬到禁止依赖 Render
的底层目录。后续应继续分离渲染租约所有者，而不是隐藏依赖或新增平行状态。

## 验证

- Windows Clang Debug 相关目标构建成功，scene-load-application、texture-load-application、
  scene-load-builder、core-boundary 和 core-boundary-negative 共 5 项通过。
- 补充数量不匹配、空租约和跨渲染服务租约拒绝用例；失败不产生准备对象。
- 既有重载、场景切换、失败保持默认版本与重试提交测试通过。
- 原生无线程静态 sc-mat-s 的 CAORS Application 构建与测试通过。
- scene_session.cpp 的 clang-tidy 无诊断；修改文件通过 clang-format 与 git diff --check。

没有修改公共 API/ABI；未执行 Linux/Web、完整安装矩阵或 GUI 手工验收。
