<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-130 作者事务与来源修订 Spike 记录

## 结论

M-130 建立了 Editor 私有的多文档事务内存参考模型，并确定真实磁盘实现使用可恢复清单，而不是把
多文件原子性建立在连续 rename 的假设上。冲突判断使用完整基线内容；FNV-1a 摘要与字节数只作为
轻量修订诊断，不承担数据正确性。

## 已验证行为

- 准备阶段一次校验所有参与文档，任一基线不符时不修改任何内容。
- 成功提交通过完整暂存副本一次发布场景与 Prefab 新值。
- 在第二项应用前注入失败后，两个文档均保持原值，并可在故障解除后重试。
- 新建文档以“不存在”作为基线，Undo 后重新变为不存在。
- Undo/Redo 在当前内容被外部修改时拒绝执行，不覆盖未知作者数据。
- 空路径、无变化项和重复目标在进入事务前被拒绝。

## 验证

- Windows Clang Debug `gneiss_editor_author_transaction_test` 构建通过。
- `gneiss.editor.author-transaction` 专项测试 1/1 通过。
- 参考模型不修改公共 API，也不改变现有场景保存行为。

真实 Native FileSystem 后端、恢复清单、启动恢复和磁盘故障注入由 M-131 接续完成。
