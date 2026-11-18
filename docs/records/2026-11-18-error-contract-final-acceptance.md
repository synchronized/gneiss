<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 错误契约收敛本地验收

## 范围

收尾 [PLAN-118](../plans/PLAN-118-error-contracts.md) P4，仅覆盖计划列出的
World/GameObject、Meta getter/ops、Render 回执与 Prefab 重放。
不表示全仓错误码审计完成，也不代表 0.52 版本发布验收。

## 残留复核

| 链路 | 保留或修正 |
| --- | --- |
| World 对象、场景 ID 查找与 borrow | 保留线程、失效 generation 和跨 World 输入诊断，属于外部身份边界 |
| create_scene/create_object/transfer_subtree/visit_objects | 保留创建失败、外部父对象、非法变换、循环及回调拒绝；不为统一签名删除 result |
| transfer_subtree 内部遍历 | 外部身份验证后，子树失效成员和归属断裂改为断言，与 children/parent 查询一致；失败回滚规则不变 |
| World 域耗尽 | out_of_memory 表示有限身份空间耗尽，不能按名称替换成堆 OOM 终止 |
| Meta member/as_*/property::get | 动态成员及能力查询保留空值/错误；有效借用的读取、写入与适配器契约已收敛 |
| Meta setter/replace、容器写入 | 仍可能输入非法、能力缺失或回调拒绝；保留候选修改与失败不提交 |
| Render 队列、预算、后端 | 保留 not_ready、显式预算拒绝和第三方结果；只对已证明的初始化/关闭回执身份断言 |
| Prefab 与 value 重放 | 组件适配器操作失败返回上层；纯 value 路径冲突继续重放；意外异常不伪装为冲突 |

本轮只修正 transfer_subtree 的两处内部契约，并修正文档中残留的 world_scenes.cpp 文件名。
没有建立平行 checked/unchecked API，没有改变资源所有权、外部输入拒绝或组件失败回滚。

## 验证

| 配置 | 结果 |
| --- | --- |
| Windows Clang Debug，共享库 | 相关目标构建通过，23/23 联合回归通过 |
| Windows MSVC Release，共享库 | 相关目标及编辑器 UI 依赖构建通过，23/23 同组回归通过 |
| Windows Clang Debug，静态库 | 四个代表性目标构建通过，7/7 回归通过 |

共享库联合回归覆盖 value 编辑、Meta 属性访问/注册/容器、JSON 与二进制归档、场景描述、
Prefab 覆盖、World/场景生命周期、变换、场景迁移、属性检查、World 渲染同步、组件适配、
编辑器组件编辑，以及 Meta/World/渲染线程的既有契约与异常子进程测试。
静态库覆盖 property_access、meta-access-contract、meta-instance、native-scene-lifecycle、
render_executor、fatal-render-frame、fatal-render-command。

没有增加仅验证实现结构的测试；场景迁移复用既有成功、循环拒绝、身份保持和卸载检查。
git diff --check 通过。P0–P4 本地实施与验收完成。

## 验收边界

Windows 本地构建与 CPU 行为回归用于本计划收尾；不声称通过 Linux/macOS/Web、MSVC Static、
GPU 设备故障或完整安装 consumer 矩阵。本轮未修改公共 ABI、导出、安装或依赖传播配置。
跨平台与完整交付矩阵仍归 [DEV-052](../plans/DEV-052-architecture-cleanup.md) A52-05，
真实 GPU 压力/恢复验收归 PLAN-115。既有历史通过记录不能代替这些未执行项。
