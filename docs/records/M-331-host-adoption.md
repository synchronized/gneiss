<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：宿主重载采用与就绪查询

2026-10-15，继续落实 [版本采用提交边界](M-331-adoption-transaction.md)。

## 实现

渲染选择域完整编码设备能力快照的 generation 与四个 sampled_transfer_formats 位，
携带格式域标记 render-prepare-v1。采取保守隔离，即使网格不使用纹理也按相同请求配置区分；
不含内容摘要，来源、URI 和类型仍属于资产身份。设备身份沿用现有 profile 契约。

texture_load_completion 返回实际准备使用的 profile。活动 World 的固定版本重载先准备
选择映射，再调用场景批量引用替换，成功后交换选择；场景拒绝时选择不提交。
候选发布本身仍不等于采用，失败清理沿用原有租约及缓存回收流程。

render_asset_loader::acquire_selected 只查询已采用版本并取得所属渲染服务的就绪租约，
未采用、配置不匹配或已回收返回 NOT_READY，不读取文件、重新准备或等待。
旧 acquire_cached 与同步加载调用者本轮未改写。

## 验证

Windows Clang Debug 相关目标构建通过，定向 CTest 5/5 通过：render_asset_loader、
texture-load-service、texture-load-application、core-boundary、core-boundary-negative。
新增测试覆盖发布不自动采用、采用被拒绝、成功查询、来源文件变坏后仍取得已采用版本、
设备代次及每个能力位隔离，缺失查询清空输出。

宿主既有测试覆盖线程池与协作执行、重复重载、准备失败、取消、ECS 冲突拒绝及重试；
缓存原语测试负责选择不提交的反例，未增加公开诊断 API 直接读取宿主私有缓存。
相关实现执行 clang-tidy，既有复杂度和格式告警保留。未运行 GPU、完整 Sponza、
跨平台与安装矩阵，未执行远端操作。

## 剩余边界

异步新场景激活还未设置初始采用选择。Editor 节点创建、Prefab 与撤销重做仍须迁移，
Application 级共享资产所有权也尚未完成。本轮不是 0.51 完成或发布验收。
