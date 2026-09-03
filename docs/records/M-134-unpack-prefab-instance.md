<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-134 Unpack Prefab 实例实施记录

## 结论

M-134 为 Editor 私有 Prefab 作者命令核心增加 Unpack。命令将目标实例的当前投影视图物化为普通
场景节点，移除该实例的 Prefab 身份，并通过 M-131 作者事务原子保存场景变更。Prefab 来源文件与
其他同源实例保持不变；界面入口、命令历史注册和运行态镜像重建由 M-135 接入。

## 已验证行为

- 原实例根转换与显示名称由新的普通包装节点承接，来源根及后代保持相对层级。
- 每个物化节点使用调用方提供的新 UUID，父子引用同步重映射，且拒绝重复、缺失或冲突的 UUID。
- 当前实例的 Translation、Rotation 和 Scale 覆盖先应用到投影，再写入普通节点。
- 目标实例从 `prefab_instances` 移除，其他实例及其覆盖不变，Prefab 来源文件不产生修改。
- 场景与来源节点的未知字段在物化后保留，输出可由真实 Scene Instance 保存重开。
- 反向作者事务可以恢复解包前的场景文档，为 M-135 的 Undo/Redo 提供可逆变更。

## 验证

- Windows Clang Shared Debug 的 `gneiss.editor.prefab-authoring` 测试通过。
- 双实例夹具验证仅解包目标实例、覆盖物化、新 UUID 层级及其他实例保留。
- 原子提交后的场景通过真实 Application 与 Scene Instance 加载，反向事务恢复后再次加载成功。
- 新增与修改源码通过 `clang-format` 和 `clang-tidy`。

M-135 下一步把 Create、Apply 和 Unpack 接入 Editor 层级与 Inspector，注册命令历史，并在提交后
重建 Editor 与 Runtime 镜像、恢复选择和报告冲突。
