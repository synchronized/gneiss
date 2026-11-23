<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 属性值接口与对象编辑收敛验收

日期：2026-11-05。环境：Windows、windows-clang-debug。

## 完成范围

- IPC 属性值编解码复用 Meta JSON 节点接口，仅保留协议字段约束、字符串长度限制和 GUID 的 `type_id` / 32 位十六进制适配。
- 组件属性读取返回 `result<value>`，通用属性访问使用 `meta::instance`；Transform 存储路径及组件自定义适配回调保留。
- 引用属性面板支持展开、编辑、复制、粘贴和清空；修改先生成独立对象，再替换引用。指向常量对象的引用保持只读。
- 组件编辑历史保存序列化快照，每次撤销、重做重新构造，避免对象共享修改污染历史。

当前接口边界见 [value 参考](../reference/value.md) 与 [ADR-104](../decisions/ADR-104-value-object-reference.md)。本批次属于 [DEV-052](../plans/DEV-052-architecture-cleanup.md)。

## 验证

- `cmake --build --preset windows-clang-debug -j5`：通过。
- 34 项针对性测试经修复及受影响项复跑后全部通过；覆盖 Meta、序列化、IPC、组件访问、编辑器与 Prefab。
- 最终复跑 7 项 IPC、运行时属性编辑及组件编辑测试：全部通过。
- 新增测试确认标量 IPC 编解码无需提前初始化默认注册表；对象编辑不修改源对象，撤销与重做不受外部对象修改影响。
- `clang-format --dry-run --Werror` 与 `git diff --check`：通过。

未运行大型场景验证或远端 CI。Prefab 标量覆盖仍拒绝对象值；材质编辑器本批次未引入引用属性。引用复制本身仍共享对象，只有显式快照操作复制内容。
