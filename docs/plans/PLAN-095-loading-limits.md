<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-095：简化资源加载额度

状态：已完成。验收见[实施记录](../records/2026-10-31-loading-limits.md)。

目标：依照 [ADR-100](../decisions/ADR-100-loading-limits.md)，删除业务加载逐层额度，
保留集中读取保护与解码格式检查。不实现流送、总内存预算或 GPU 进度。

- 移除 load options、Decoder、管理器与资源服务额度参数及缓存键中的上限。
- 文件服务增加使用配置上限的 read 重载，原显式收紧入口不变。
- 清理场景编排和解码采用层的额度传递；保留统计溢出与 PNG 展开保护。
- 更新调用方、相关测试和当前行为文档。
- 验收 Debug 编译、相关 CPU 测试，不运行 GPU、大场景或远端 CI。
