<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-117：功能命名空间收敛

## 状态
2026-11-11：已完成。用户确认移除 gneiss::engine 中间层，保留宿主/编辑器等领域命名空间。

## 决策与范围
见 [ADR-120](../decisions/ADR-120-domain-namespaces.md)。目录、构建目标和显式类型 GUID 不变。
meta/events/math/serialization/logging/files 迁至 gneiss 下同名领域；通用值和现有服务类型迁至 gneiss。
任务调度接口整体迁至 gneiss::tasks，内部 detail 留在 tasks 内；不同时重命名类或搬目录。
保持 gneiss::host/editor/runtime 等既有领域，不建立旧命名空间别名。
历史 Record/已发布 Version 不改写；当前 Guide/Reference/Concept 和构建规则同步更新。

## 实施与验收
- [x] 全量迁移源码、调用点、模板特化、测试及当前文档。
- [x] 核查 detail 合并、同名解析及 ADL；显式 GUID 不变。
- [x] 更新命名空间边界规则及反例。
- [x] 构建 Engine、Runtime、Editor、工具与测试；定向 CPU 验证任务、Meta、序列化、日志和 World。
- [x] 检查旧名称残留，提交完整迁移；未执行平台明确记录。

不增加行为兼容分支，不运行 GPU 场景或远端 CI。此迁移要求所有 C++ 消费者重编译。

## 验收结果

- Windows Clang Debug 完整默认目标构建通过（Engine、Runtime、Editor、工具、示例及测试程序）。
- 28 项定向 CPU 测试通过：任务调度、任务头可见性、日志与事件、基础身份、数学、World、Meta 与序列化。
- `check_core_boundary.cmake` 及 `test_core_boundary.cmake` 正反例通过。
- 源码、测试和示例无旧命名空间残留；所有改动 C++ 文件的 UUID 字面量与迁移前一致。
- CMake 的 `gneiss::engine` 目标仍保留；它不是 C++ 命名空间。
- `git diff --check` 通过。未运行 GPU、浏览器、Linux、MSVC 或远端 CI 验证。

PLAN-116 尚待完成的日志宿主接入不属于此次命名空间迁移的验收范围。
