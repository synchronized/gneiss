<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：同步入口审计与 Runtime 回退移除

2026-10-15，依据 [ADR-064](../decisions/ADR-064-shared-assets-and-scene-instances.md)
审计交互调用者并完成首批收敛。

## 审计结果

| 调用者 | 本轮后的状态与约束 |
| --- | --- |
| Editor/Runtime 常规场景加载器 | 已有异步准备与推进入口，但不代表其他重开场景路径也已异步化 |
| Runtime 网格、材质、纹理重载 | 仅允许异步 begin/poll；装配缺失返回失败，不执行同步 apply，不推进修订 |
| Runtime Scene/Prefab 结构重载 | 暂保留单项同步适配；混合结构与渲染资产批次拒绝 |
| Editor 资产替换、Prefab 放置与恢复 | 仍调用同步实例接口；撤销重做也包含相关调用 |
| Editor 文档事务 apply_author_documents | 提交文件后同步 session.open，失败时写回并尝试重开旧文档 |
| Editor 作者资产文件监听 | 当前场景或引用的 Prefab 改变时同步 session.open，成功后清空历史 |
| 测试与基准中的 session.open | 仍有同步调用，需要按测试目的迁移，不据此保留第二套加载实现 |

Editor 的迁移需要覆盖迟到回执、目标失效、失败回滚与历史提交时机，不能简单将返回码
改为 not_ready，也不能在主线程阻塞等待。普通获取尚未接入已采用版本索引，
Application 级共享资产所有权也仍未完成。

## 本轮实现

runtime_asset_reloader 先按资产类型选择路径。渲染资产缺少任一异步回调时明确失败；
异步开始失败也不调用同步适配。main.cpp 删除渲染资产同步重载及逐节点重新设置路径，
仅保留 Scene/Prefab 结构适配。结构与渲染资产混合批次返回参数错误。

## 验证

Windows Clang Debug 构建 gneiss_runtime 与 gneiss_runtime_asset_reloader_test 通过。
定向 CTest 共 4/4 通过：runtime.asset-reloader、texture-load-application、
core-boundary、core-boundary-negative。

新增覆盖三类渲染资产在回调缺失或仅装配一半时拒绝同步回退、异步开始失败后重试、
混合批次拒绝及失败不推进修订。保留异步成功、排队、完成失败和结构适配验证。
两个 Runtime 实现文件执行 clang-tidy，仍有既有格式等告警；不宣称静态检查零告警。
未执行 GPU、完整 Sponza、跨平台与安装矩阵，未执行远端操作。

后续顺序见 [开发计划](../plans/DEV-051-architecture-boundaries.md#同步入口收敛顺序)。
