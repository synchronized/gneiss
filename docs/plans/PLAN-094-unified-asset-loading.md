<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-094：统一 CPU 资源加载

状态：已完成。决策见 [ADR-099](../decisions/ADR-099-unified-cpu-loading.md)，
验收见[实施记录](../records/2026-10-31-unified-cpu-loading.md)。

## 目标与边界

统一 load<T>、asset_request<T> 和共享只读 CPU 结果；网格/纹理步骤迁至私有 Loader。
缓存统一使用弱引用，完整加载参数参与身份。复用文件服务和现有任务执行器。
不增加线程池、GPU 进度、流送或新的版本事务；不改变 resource_service 与 Render 的职责。

## 实施清单

- [x] 核查上传所有权，纹理准备新增共享只读入口，避免复制大块数据。
- [x] 统一结果交付和统计查询，加载、查询、采用使用相同参数身份。
- [x] 全部类型使用弱缓存；相同键合并在途请求。
- [x] 迁出格式状态与下一步读取决策，管理器统一调度。
- [x] 迁移消费方，删除 request_data、asset_data_request 和专用一次性加载方法。
- [x] 验证取消、失败重试、共享、释放、参数隔离及不复制交付。

## 验收条件

Windows Clang Debug 编译与相关 CPU 测试通过；暂不运行 GPU、大场景或远端 CI。
具体 API 与限制由[资产参考](../reference/assets.md)维护，不在本计划重复定义。
