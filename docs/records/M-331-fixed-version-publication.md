<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：固定版本候选 RID 与原子发布

2026-10-15，实施 [共享资产方案](../plans/asset-scene-ownership.md) 的加载器发布边界。

## 实现

render_asset_loader 的 stage_asset 增加显式 fixed_version 模式，默认仍是原热重载模式。
固定版本已发布时复用原租约并标记 existed，调用方应跳过上传；新版本创建独立 RID，
不使用 URI 查询到的旧 RID。材质准备结果保留每个槽位的确切纹理版本，暂存材质时核对
依赖版本，不能只凭 URI 绑定其他候选。材质租约持有纹理租约。

resource_cache 增加 publish_versions，整批校验并复制映射后交换发布；同键只接受同一条目，
竞争候选返回 INVALID_STATE，失败不留下部分映射。缓存映射持有 const entry，候选条目
本身直接进入缓存，不另外复制条目导致依赖引用分叉。固定版本发布不调用资源 replace。
事务暂沿用复制缓存映射的方式，不宣称大缓存下具有固定时间上界。

两种发布模式不得混用。GPU 上传仍由调用方完成确认或撤回后再处理候选寿命；这次没有
改变上传协议。未发布候选销毁会释放独立 RID，其他场景的已有租约不受影响。

## 验证

Windows Clang Debug 构建与定向 CTest 7/7 通过：resource_service、render_asset_preparation、
texture-load-service、scene-load-application、render_asset_loader、core-boundary、
core-boundary-negative。

新测试覆盖同版本 RID 复用、新版本独立 RID、同键竞争拒绝、材质依赖版本错误、未发布
候选销毁、整批失败无部分缓存、旧资源数据保持，以及材质租约延长纹理寿命和最终释放。
测试直接调用加载器，不代表真实 GPU 上传或两个场景已接入。格式及 diff 检查通过。
静态检查仍报告加载器暂存/发布函数复杂度等告警，不能视为全文件检查通过。

## 剩余工作

场景异步加载尚未启用 fixed_version：需接入同版本跳过上传、候选撤回、返回确切版本租约，
再调整场景构建读取与缓存归属。当前不得将 URI 查询路径悄悄改为某个“最新版本”。
共享任务订阅、取消隔离及跨场景端到端复用仍未完成。
未执行完整 Sponza、Linux、安装消费者或发布矩阵，未进行远端操作。
