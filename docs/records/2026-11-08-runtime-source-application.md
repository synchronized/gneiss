<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Runtime 来源刷新迁移记录

日期：2026-11-08。对应 [PLAN-105](../plans/PLAN-105-game-object-runtime-model.md) 与
[ADR-110](../decisions/ADR-110-game-object-runtime-model.md)。

## 变更

通过资产加载的场景，包括含 Prefab 的场景，统一按组合后的有效对象创建。
Runtime 场景服务接收激活回执，持有固定来源内容和对象地址到运行成员的关联。
场景/Prefab 来源刷新复用共享组合与应用，不通过运行对象捕获作者数据。
重载回执显式返回对象关联，宿主直接交给 Inspector。
来源未修改的变换保留游戏运行值，来源变更才覆盖；失败应用使比较基线失步。
解析失败发生在应用前，不影响上一次成功基线。

Prefab 展开成员全部生成运行 UUID，包括根节点。根角色切换不替换仍存活成员的实体。
Editor 的创建、子树恢复与 Prefab 放置完成回调改用文档投影定位，
删除以文档 UUID 查询运行 UUID 的残留依赖。

## 验证

新增 Runtime 激活场景测试，覆盖无运行 Prefab 元数据、来源成员改名/增删、
未改来源时保留游戏变换、根角色切换保持实体、坏文件失败及修复后继续刷新。
根 UUID 调整曾暴露编辑器恢复回调的错误查找，已将该调用改为投影定位后重新验证。
最终 Windows Clang Debug 默认构建通过；84 项 Scene/Prefab/World/组件/Editor/IPC
及边界回归全部通过（81.69 秒）。构建成功后才运行测试。
日志：`build/plan105-runtime-sources-verified-build.log`、
`build/plan105-runtime-sources-verified-tests.log`。`git diff --check` 通过，
修改行 clang-format 检查无进一步变更。

## 未完成

手动 instantiate_prepared 的 Prefab 分支与未交接激活回执实例的重载仍走旧实现。
运行捕获、子树恢复及来源分类仍需连同调用方和测试删除；不能将正式路径迁移视为整个
World 已无 Prefab 状态。持久父关系的对象地址迁移也尚未完成。
未运行 GPU 大场景和远端 CI，没有远端推送或发布。
