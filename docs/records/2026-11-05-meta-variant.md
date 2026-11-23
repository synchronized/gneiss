<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Variant 与 Prefab 属性覆盖反射验收

新增 variant_ops、variant_view 与显式 variant_serialization_traits 标签；JSON 使用 $case/$value，
未配置标签仍可借用访问，JSON 返回 unsupported。注册表冻结与 variant 实例创建检查全部候选分支。
分支默认构造失败保留标准 variant 语义；无值状态可以检查并通过 select 恢复。
当前接口见[Variant 分支反射](../reference/reflection-containers.md#variant-分支反射)。

Prefab 属性覆盖直接反射 key/value，路径使用 property/index 标签；engine::value 保留 kind/value。
删除 make_prefab_overrides、parse_prefab_override 及最后的 scene_codec.cpp、description_codec.hpp。
迁移测试资产、示例场景与格式文档，不兼容未发布的旧平铺覆盖及旧路径格式。
当前格式见[场景格式](../reference/scene-format.md)。

Windows Clang Debug 全量构建通过。14 项相关测试通过，另有 core-boundary-negative 通过：
覆盖标签往返与重排、嵌套容器/optional、未知标签、未配置标签、不可默认构造分支、
未注册分支、valueless 状态恢复、Prefab 路径与编辑器属性覆盖操作。
首次验证发现并修复 variant 创建未验证未选中分支的问题；场景值错误诊断更新为完整嵌套路径。
格式检查、git diff --check 和分层检查通过。
未运行大场景、完整测试矩阵或远端 CI，未新增编辑器 variant 分支切换控件。
