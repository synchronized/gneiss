<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：场景资源引用批量切换

2026-10-15，实施 [ADR-064](../decisions/ADR-064-shared-assets-and-scene-instances.md) 的使用者更新方向。

## 实现与前置条件

scene_instance_service 增加内部 replace_render_assets，输入旧、新租约对。先验证所属线程、
类型、URI、渲染服务归属及重复旧 RID，再收集此 World 中全部场景普通节点与 Prefab 节点。
仅匹配仍持有指定旧 RID 的使用者；不更改描述、对象身份或其他 World。
纹理变化须先形成依赖它的新材质，再提交材质替换，不能只替换纹理缓存。

World 内部接口 replace_mesh_renderers 先验证整批实体和组件旧值；全部通过后直接替换
已有组件数据，不分配组件。随后场景服务移动租约，提交阶段不分配也不抛异常。
失效实体、缺失组件或旧 RID 不一致不会导致前面节点先提交。资源 RID 的有效性和所属
渲染服务由场景层租约验证负责，World 仍仅处理纯数据。

RID 表增加按类型遍历存活值的内部方法，回调不得修改槽位集合；没有新增实例注册表。
调用者必须在安全帧边界调用，并保证新资源上传已确认。本次未加入宿主回调或自动通知。

## 验证

Windows Clang Debug 构建及定向 CTest 9/9 通过：world_state、scene-load-builder、
scene-load-application、scene_instance、scene_prefab_instance、scene_structural_reload、
prefab_structural_reload、core-boundary、core-boundary-negative。

新增测试覆盖普通节点和 Prefab 统一切换、第二个使用者冲突时第一个保持原值、跨线程拒绝、
重复旧 RID 拒绝、跨渲染服务租约拒绝、其他 World 保持旧材质，以及释放旧引用后的资源回收。
这些测试通过模拟资源服务验证 CPU 租约及 ECS；不代表真实 GPU 帧在途测试。

World 和场景服务执行 clang-tidy，新增初始化告警已修正，文件原有复杂度和参数可互换等
告警仍存在。格式与 diff 检查通过。未执行跨平台、安装消费者及完整 Sponza 矩阵。

## 后续实施：旧版本租约快照

同日补充 `capture_render_assets`，从此 World 各场景普通节点与 Prefab 的实际租约
采集网格和材质，按 RID 去重。同 URI 的多个版本分别保留，不通过 URI 缓存猜测旧版本，
不执行文件读取。快照持有资源至调用方释放；失败不覆盖调用方原有输出，跨线程调用拒绝。
它不采集裸 ECS RID 使用者，也不保证采集后使用者未变化；提交仍走上述事务验证。

补充测试覆盖空场景、空租约转换、不同版本共存、实例卸载后的重新采集、仅 Prefab 使用、
无文件 I/O、跨线程拒绝保持原输出，以及切换后快照维持旧资源寿命。
重新构建相关目标后，Windows Clang Debug 定向 CTest 8/8 通过：render_asset_loader、
scene-load-builder、scene_instance、scene_prefab_instance、scene_structural_reload、
prefab_structural_reload、core-boundary、core-boundary-negative。
对渲染资产加载器和场景服务执行 clang-tidy，现有复杂度等告警仍存在，新增方法无告警。
没有执行真实 GPU、跨平台或完整 Sponza 验证，没有改变宿主当前重载入口。

## 剩余工作

宿主须把上传完成结果转成旧、新租约对，在安全边界提交更新，并处理失败/取消与回收。
随后启用场景固定版本加载，继续迁移 Application 级共享资产所有权。更新通知与全链路
热重载尚未完成，未进行远端操作。
