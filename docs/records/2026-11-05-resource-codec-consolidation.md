<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 材质与输入映射 codec 收敛验收

删除 material_codec.cpp/.hpp 与 action_map_codec.cpp/.hpp，调用点使用 Meta 通用编解码。
资源内部 register_reflected_asset 复用类型 builder，统一追加格式包装并提交，删除各类型
重复的四个格式转发函数。格式名、版本、输入映射 1 MiB 限制、未知字段策略及业务校验不变。
构建源列表、内部接口头列表和分层脚本同步清理失效路径。

Windows Clang Debug 相关 Engine、Editor、导入器和测试目标编译通过。
8 项测试通过（1.60 秒）：assets.manager、assets.material、tooling.asset_writer、action_map、
render_asset_loader、texture-load-service、serialization、editor.material-editor。
核心分层检查与 git diff --check 通过。源码、测试和构建配置未残留旧 codec 引用。

未运行大场景、完整测试矩阵或远端 CI。本批不变更 Scene/Prefab 和二进制 codec。
当前调用入口见 [资产参考](../reference/assets.md#描述编解码入口)。
