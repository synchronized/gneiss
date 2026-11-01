<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：宿主异步重载切换场景版本

2026-10-15，接续[场景版本切换](M-331-scene-version-switch.md)，落实
[ADR-064](../decisions/ADR-064-shared-assets-and-scene-instances.md) 的活动 World 使用者更新。

## 本次实现

宿主 request_render_assets 和 request_textures 的 reload=true 路径委托 scene_session：

1. 从当前场景实际租约采集旧版本，不查询 URI 缓存猜测旧 RID。
2. 请求纹理时，检查这些材质版本持有的五个纹理槽，把受影响材质加入同一准备批次。
   来源读取、解析和复验仍由既有异步服务处理，不在宿主扫描文件。
3. 使用固定版本候选，上传完成才发布；相同版本复用已有资源。
4. poll_textures 在所属线程、帧快照捕获前，将回执转换成旧、新租约对并调用场景批量事务。
   普通节点和 Prefab 仍引用旧 RID 时一起切换；成功回执代表场景切换已经完成。
5. 准备、上传或场景提交失败不切换场景；场景冲突把回执改为失败并清空返回租约和版本。
   请求释放旧快照，缓存回收无使用者条目。上传候选的后端数据沿用既有按引用清理机制，
   不提前破坏仍在途的帧数据。commit_ms 增加场景提交与本次缓存回收耗时。

没有新增全局“最新版本”表或第二个缓存。请求快照仅保留到完成或关闭。
重复 URI、并发批次和扩展后的批量上限仍由入口/既有服务拒绝，不拆成部分成功批次。

## 验证

Windows Clang Debug 相关目标及 Runtime、Editor 构建通过，定向 CTest 7/7 通过：
scene-load-application、texture-load-service、texture-load-application、scene-load-builder、
asset-pixel、core-boundary、core-boundary-negative。

宿主新增测试在协作执行和线程池两种模式覆盖：仅请求纹理时补齐材质、相同来源版本复用、
材质损坏、取消、裸 ECS 改动引起提交冲突、失败回执不泄漏候选，以及卸载冲突实例后重试。
现有真实 GPU 像素测试覆盖纹理颜色更新、网格与材质联合更新、损坏资源保留旧画面和 resize；
窗口按测试既有配置隐藏。没有运行完整 Sponza、跨平台和安装消费者矩阵。

scene_session、application_host 和 render_asset_loader 执行 clang-tidy；新初始化告警已修正，
原有加载器复杂度等告警保留，不宣称静态检查零告警。

## 迁移边界

- 目前更新范围为请求前快照中的版本及当前活动 World 仍使用这些 RID 的节点；裸 RID
  使用者不自动跟随，跨 World 订阅未实现。
- reload=false、同步资产重载及新场景资产准备仍使用旧 URI 路径。后续创建的实例可能从
  旧 URI 缓存取得不同版本；本次不建立隐式最新版本映射来掩盖这项迁移缺口。
- 下一步需统一新场景固定版本选择，再迁移 Application 级共享资产所有权。全链路通知、
  跨域共享和完整关闭/性能验收尚未完成，0.51 未完成发布。
- 本次没有远端或 Granit 操作。
