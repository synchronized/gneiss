<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 渲染资产渐进加载首批审计与契约验证

日期：2026-10-22。对应[实施计划](../plans/PLAN-051-progressive-render-assets.md) S0 与 S1。
本批完成源码消费者分类和单纹理可控上传契约测试，没有改变生产加载或绘制行为。
尚未完成性能基线、真实 GPU 延迟上传及首次占位显示，不能称为渐进加载已接入。

## 消费者及迁移处置

| 入口 | 当前用途 | 处置 |
| --- | --- | --- |
| `runtime/loading/scene_load_service.cpp` | 新场景准备使用 fixed_version，收集 versions，构造实例前 prepare_adoption，接受场景时 commit | 迁移：场景结构激活与视觉资源完成分离；先审计模块初始化依赖，不能直接删等待 |
| `runtime/loading/scene_session.cpp` 的 submit/poll_assets | 捕获旧引用，纹理更新扩展关联材质，固定版本完成后批量替换场景引用 | 迁移：稳定 RID 更新后删除普通热重载的旧/新引用遍历；依赖纹理更新通知下沉渲染侧 |
| 同文件 `accept_edit_assets` | 编辑回调成功后提交默认版本选择 | 普通路径删除目标；编辑事务和特殊快照另行分类后移除该门 |
| `runtime/application.cpp::prepare_edit_assets` | Editor 请求固定版本候选 | 随命令迁移，不直接切为原位更新，否则预览/取消可能修改共享对象 |
| `editor/commands/editor_asset_edit.*` | 保存 previous/next Mesh 绑定，支持恢复确切旧资源 | 暂时保留，先确定命令撤销是恢复作者引用还是旧内容 |
| `editor/commands/editor_prefab_edit.cpp` | 两处依 completion.versions 决定是否通过 accept_edit_assets | 迁移调用边界；Prefab 结构快照不等于通用资产历史库，不应一并删除 |
| `render/render_asset_loader.cpp` | 双发布模式、固定版本复用、默认选择查询及旧同步入口 | 消费者迁移前不整体删除；先保留稳定 RID 更新与共享生命周期 |
| `resource/cache/resource_cache.*` | 同时服务渲染和 Prefab 的版本及采用功能 | 禁止按渲染消费者减少就整体删除；逐域收敛后清理无用接口 |

## 可复用机制与真正的阻挡点

1. `render_resource_service::replace_texture` 已保留 RID 并切换共享只读数据；已有帧引用可保活旧数据。
   新测试证明单纹理旧模式在回执前不发布，失败不覆盖旧内容，成功后同 RID 指向新数据。
2. `render_frame_packet.cpp::capture_resource` 对无数据句柄返回 INVALID_HANDLE；
   Granit `execute_frame` 又假定 Mesh、材质和纹理均存在。首次占位需要从请求、快照到绘制贯通，
   不能单改后端的 return 为 continue，也不能把已销毁或跨服务 RID 当成“加载中”。
3. `release_invalid_textures` 在 CPU 来源指针变化时删除旧纹理镜像并清空材质镜像。
   若提前暴露新 CPU 数据，将破坏继续画旧 GPU 内容的目标。后续必须保留旧有效镜像直到新镜像
   可安全替换，并同步更新材质借用的纹理视图。
4. 固定版本加载只按 URI 切成旧模式不够：旧模式查询 URI 表，当前场景可能只持有版本表中的条目。
   必须迁移身份索引/既有使用者绑定，否则会产生无人使用的新 RID。

## 上传契约核对

检查实际构建缓存中锁定的 Granit 头文件，而非仅依赖相邻 checkout。
`include/granit/renderer/upload_batch.h` 明确约定同步 submit 成功返回时 GPU 复制完成；
异步 submit 的终态说明后端不再需要 Batch 暂存数据。
Gneiss 当前 create_texture_mirror / create_mesh_mirror 使用同步 submit。
因此宿主 poll_texture_preparation 虽然查询的是渲染命令回执，其命令内部仍可能等待 GPU 复制。
不能把现有回执简单改名为 GPU fence，也不能删除它后宣称上传已无阻塞。
后续需核对异步上传与后续绘制排序、错误、暂存和销毁契约，再接到已有渲染命令队列。

## 新增确定性验证

`tests/render/texture_load_service_test.cpp::delayed_single_texture_update` 使用协作执行器、
内存来源和手动回执，无真实 GPU、随机睡眠或时间等待：

- 首次上传预算不足时没有成功缓存，也不会提前报告完成。
- 上传一旦被接受，多轮 advance 不重复提交，后端持有所需数据至回执。
- 延迟更新期间当前资源不变；失败保旧且不会自动每帧重试。
- 显式再次更新成功后保留 RID，旧帧数据仍可读。
- 释放缓存和租约后 RID 失效，旧帧持有的数据仍独立有效。

Windows Clang Debug 目标构建通过；`gneiss.texture-load-service` 与
`gneiss.texture-load-application` 两项测试通过（2/2），格式与 diff 空白检查通过。
本测试是后续重构需要保留的行为基线，
不验证占位像素、GPU 异步顺序，也不是加载耗时或显存性能基线。

## 下一步

先核对 Granit 异步上传契约并以单纹理实现渲染侧待更新记录、旧镜像保留及完成切换；
随后贯通“合法但未就绪”与“无效 RID”的区别，并加入首次默认纹理的真实绘制测试。
在这两个闭环完成前，不移除场景等待，也不删历史快照。
