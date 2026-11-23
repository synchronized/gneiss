<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Meta 默认注册表入口收敛验收

补齐只读和可写 instance::from_object、枚举注册与查询、属性绑定查询的无 registry 入口。
视图字节编解码及内部 JSON DOM 适配增加使用视图绑定注册表的入口，不重新获取默认表。
显式注册表接口和内部递归上下文保留，启动仍由 initialize_meta 负责。

Scene 组件描述、覆盖 codec、编辑器属性修改与材质面板已移除直接获取默认注册表的代码。
业务测试改用无 registry 入口；Meta 注册表机制测试保留显式访问。
核心分层脚本新增禁止业务源码调用 default_registry 的规则及反例验证。
当前规则见[默认注册表与启动](../reference/reflection.md#默认注册表与启动)。

验证结果：

- Windows Clang Debug 全量编译通过。
- 15 项相关测试通过（2.23 秒），包含默认注册表初始化成功、失败及异常，隔离表视图
  字段名保持，Scene/Prefab、组件和编辑器属性操作。
- 核心分层检查及反例脚本通过，改动源码已 clang-format，git diff --check 通过。

未运行大场景、完整测试矩阵或远端 CI。
