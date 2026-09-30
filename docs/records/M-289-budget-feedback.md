<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-289：场景预算反馈验证

日期：2026-09-30。范围：0.44 开发分支预算反馈，不代表版本发布完成。

## 交付

Application 场景进度采样共享资源账本和在途上传估算，Runtime 将其放入可选 IPC 预算对象，
Editor 场景加载状态展示候选逻辑容量、CPU 数据、Application 合计、剩余额度及上传当前值和峰值。
终态保留结束时快照。统计范围、字段和兼容语义见
[ADR-052](../decisions/ADR-052-texture-residency-and-budgets.md)。

## 验证

Windows Clang Debug 全量构建通过。共享库相关 4 项测试通过（2.62 秒）：场景 IPC 协议、
Editor/Runtime 场景加载进程联测、Application 场景加载与纹理加载服务。
静态库协议与 Application 场景加载测试 2/2 通过（0.29 秒）。
修改行静态检查通过；新增协议解析复杂度告警已通过拆出预算解析函数修复，协议文件复查无告警。
差异格式和空白检查通过。

- IPC 覆盖省略预算对象的旧消息、全部字段往返和 UINT64_MAX 精度。
- 非法负数、小数、null 及超出 64 位范围的字段均被拒绝，失败不改写输出。
- Application 测试检查候选与合计计数、可用额度关系和 ready 阶段上传预留归零。
- 进程联测验证 Editor 收到 Runtime 终态预算，不只验证 JSON 编解码。

## 限制

未人工验收界面排版，未实测完整 4K Sponza 的内存峰值。详细所需/可用字节的错误诊断、
全部临时分配之前的统一预留与全版本矩阵尚未完成；不将资源账本描述为真实 RAM/显存测量。
