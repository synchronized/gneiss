<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 资产与场景边界收尾审计

日期：2026-11-08。对应 [PLAN-102](../plans/PLAN-102-resource-object-loading.md) 与
[DEV-052](../plans/DEV-052-architecture-cleanup.md)。本记录交付现状核对、确定冗余清理与本地验收，
不把尚未实现的通用依赖能力或完整版本验收标成完成。

## 代码证据与处理

| 位置 | 当前职责与结论 |
| --- | --- |
| src/engine/function/asset/asset_manager.cpp | resource_state 只有一个 resource_cache；统一拥有五类资产及加载操作，未保留第二个资源服务 |
| function/asset/detail/asset_entry.hpp | 来源域、URI、类型、选项组成 key；cached 弱引用与 loading 共用记录 |
| function/asset/detail/load_operation.hpp | 请求观察状态、当前操作采用权、取消及迟到完成；执行队列不是独立缓存 |
| function/asset/loaders/scene_loader.cpp | Scene/Prefab 最终返回资产对象；Scene 加载必要 Prefab，不创建 World 或等待视觉资源 |
| resource/scene.hpp、prefab.hpp | 顶层实际资产类型；身份和固定内容快照分别保活，旧快照不跟随独立重载改变 |
| function/scene/scene_preparation.* | 同步内存校验、固定来源及视觉需求清单；删除无消费者的 parent_first、maximum_depth 和 asset_manager include |
| function/scene/scene_application.cpp | 运行应用负责按有效父关系创建对象；准备结果不再维护第二份持久排序 |
| function/asset/detail/asset_construction.hpp | 私有 RID 归属弱索引与释放队列仍有用途，不作为 URI 缓存或公开 RID 接管入口 |
| runtime/application.cpp | request_shutdown/advance_shutdown 异步排空场景、资产和文件；shutdown 保留同步销毁兜底，不能误称完全无阻塞关闭 |
| runtime/application.hpp | create、capture_frame 等仍有自然成功值输出参数；全仓 result 接口收敛尚未完成 |

五个主头和类型加载器均已到位。公开 request_data、prepare_scene/prepare_prefab、reserve_resource
及按 RID 找回资源的入口已删除。保留 prepare_loaded_scene/make_instantiation_data：它们处理
已加载快照，不读文件、不访问资产管理器、不推进任务；名称含 prepare 不代表旧异步加载协议。
场景父关系校验继续执行，诊断路径保留；测试改验描述顺序、非法层级和输出失败清空行为，
不再检查无人消费的排序数组。

## 未完成项

1. asset_reference_context::record/resolve 目前仅接受纹理、网格和材质；任意 asset 派生对象的
   引用解析与加载依赖扩展仍缺失。object_loader 只调用 decoder，尚无通用依赖阶段。
2. 没有可证明的通用依赖图及直接/间接循环 URI 链检测；现有固定类型依赖和禁止嵌套 Prefab
   不能代替这个能力。PLAN-102 对应验收项保持未完成。
3. 资源引用的后端一致性、描述保留边界和失败重载语义需要按完整矩阵验收，不能用负载 codec
   测试替代任意资源图测试。
4. A52-02 仍有 Application 自然值输出接口；A52-05 的 MSVC/Linux、共享/静态、Sanitizer
   和 GPU 工作流尚未完成。本次不改公共 SDK 或跨仓库 Granit。

DEV-052、VER-052 与资产/布局参考同步实际边界：不再把 Resource Service、运行 Prefab 绑定、
旧 VFS 或 Runtime 作者捕获描述为当前所有者。历史阶段记录保留，计划状态指向本次审计。

## 验证

Windows Clang Debug 默认构建通过，73 项资产/场景/Prefab/序列化/编辑器及边界相关回归全部通过
（75.45 秒）。改动行格式化和 git diff --check 通过。日志：build/plan102-audit-build.log、
build/plan102-audit-tests.log。未运行 GPU 像素/窗口、大场景、跨平台远端 CI 或发布。
本次验收完成上述审计与冗余清理，不替代 PLAN-102 剩余能力或 DEV-052 完整平台验收。
