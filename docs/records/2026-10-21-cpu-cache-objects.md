<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# CPU 文档缓存对象收敛

日期：2026-10-21。对应[模块布局计划](../plans/PLAN-051-module-layout.md)。

## 变更

- 将 cpu_document_cache、cpu_document_batch 收敛为具体 final 类，删除隐藏派生类与 make_cpu_document_cache 工厂。
- asset_manager 与测试直接构造缓存；删除所有者复制能力，批次只能从缓存 begin 创建。
- 原版本状态仍由缓存与批次共享，批次仍持有各自候选；没有引入第二套缓存或请求状态。
- 保留原线程交接、来源复验、确切版本选择和 GPU 资源生命周期。

## 验证

Windows Clang Debug 完整构建通过；10 项相关测试通过：材质文档、场景准备、Prefab 准备、
渲染资产准备、纹理服务、纹理采用、场景构建、边界正反例及内部头可见性。
新增实际生命周期测试覆盖缓存外壳先销毁、两个存活批次继续复用同一版本，
以及所有批次销毁后租约继续保持文档有效。既有来源变化/缺失、失败不发布测试继续通过。
Emscripten gneiss_engine 静态库构建通过，未运行浏览器界面。

日志在本地 build/cpu-cache-object-*.log。公共 ABI、安装配置和依赖传播未修改，
未重跑安装矩阵或图形窗口冒烟测试。

## 剩余项

本批只收敛对象层次，不代表同步缓存发布与材质/Mesh 加载路径已统一；这些工作仍在原计划中。
