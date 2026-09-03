<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-133 将实例覆盖应用到来源实施记录

## 结论

M-133 扩展了 Editor 私有 Prefab 作者命令核心。Apply 从指定实例读取已有 Transform 字段覆盖，
将值写入 Prefab 来源节点，清除该实例已提交的覆盖，并生成来源文件与场景文件两项原子事务变更。
计划同时返回全部同源实例 UUID，供 Editor 执行统一刷新；界面入口和执行反馈由 M-135 接入。

## 已验证行为

- Translation、Rotation 和 Scale 覆盖按稳定来源节点 UUID 与字段 ID 写入 Prefab 来源。
- Apply 不修改实例根 Transform、场景父级、其他实例的独立覆盖或无关来源节点。
- 目标实例覆盖在场景作者数据中清空；全部同源实例被列入刷新集合。
- 场景和 Prefab 的未知字段在转换后保留，且输出可由真实 Scene Instance 重新加载。
- 场景引用 URI 必须与 Prefab 文件路径一致；目标实例不存在或无覆盖时明确拒绝。
- 当前 Schema 之外的属性类型和字段返回 `unsupported`，不静默丢弃未来覆盖。
- M-131 完整基线检查负责拒绝外部来源修改；反向事务可同时恢复来源与实例覆盖。

## 验证

- Windows Clang Shared Debug 的 `gneiss.editor.prefab-authoring` 测试通过。
- 双实例场景验证目标覆盖清除、来源值更新、另一实例覆盖保留及同源刷新集合。
- 原子提交后的场景和 Prefab 通过真实 Application 与 Scene Instance 加载。
- 新增与修改源码通过 `clang-format` 和 `clang-tidy`。

M-134 下一步实现 Unpack；M-135 将 Create、Apply、Unpack 注册到命令历史，并依据 Apply 计划刷新
当前 Editor 会话中的全部同源实例，在刷新失败时提交反向事务。
