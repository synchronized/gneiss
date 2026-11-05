<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 序列化 context-first 验收

日期：2026-11-05。环境：Windows、windows-clang-debug。

## 完成范围

- 统一 context 管理注册表选择、深度限制及私有递归状态。
- 通用与 JSON 入口、reader/writer 格式扩展、节点及字节回调采用 context-first。
- 保留无 context 便捷入口；移除直接传 registry 的序列化重载及 JSON 专用上下文。
- 资源包装、测试与接口文档同步迁移；持久化格式不变。
- 嵌套回调共享上下文，字段委托保持当前层级，完整入口重入增加深度。
- 显式 context 与视图注册表不匹配时拒绝；失败后可以顺序复用上下文。

当前接口与生命周期见[序列化参考](../reference/serialization.md)，决策见
[ADR-105](../decisions/ADR-105-serialization-format-boundary.md)。

## 验证

- `cmake --build --preset windows-clang-debug -j5` 通过。
- 33 项相关回归通过：通用内存后端、JSON/Meta、Scene/Prefab、资产、IPC、组件与编辑器。
- 核心边界、核心边界负向、反射边界、内部头可见性 4 项检查通过，共 37 项。
- 新增用例覆盖自定义深度、节点字段委托、字节回调重入、循环失败后复用、
  编解码注册表不匹配、目标未被修改及显式空注册表拒绝。
- clang-format 检查和 `git diff --check` 通过。

未运行大型场景或远端 CI；未执行远端操作。
同一 context 不支持并发使用；没有增加通用二进制后端、流式 writer 或对象身份解析系统。
