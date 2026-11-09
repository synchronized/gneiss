<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-108 格式无关序列化入口

## 实现

serialization.hpp 的格式无关接口统一为 write、read_into、decode/decode_object，
旧 serialize/deserialize 入口删除。write/read_into 返回 result<void, diagnostic>，
decode 返回独立候选；JSON 入口及读写 context 分离仍待迁移。
Core 补充 result<void, E>，成功不构造 E，支持不可默认构造及仅可移动的错误载荷。
原有 result<void> 的引擎 error 行为保持。

## 验证

编辑器和所有包含该头的 CPU 消费目标编译通过。
native_result、serialization、serialization-archive、reflection-json、meta-container、
component-description 共 6 项测试通过。
覆盖自定义错误生命周期、成功状态切换、read_into 成功写入、默认视图重载、
后端失败诊断、循环引用与上下文复用。
扩展回归同时发现并修复自定义组件 codec 的继承字段快照漏洞，见独立记录。
构建日志：build/plan108-archive-api-build.log、build/plan108-archive-api-consumer-build.log、
build/plan108-archive-api-header-consumers.log。
未执行 GPU、大场景或远端操作；git diff --check 通过。
