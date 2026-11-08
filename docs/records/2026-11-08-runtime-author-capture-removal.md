<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 删除运行集合的作者捕获与文件编码入口

日期：2026-11-08。对应 [PLAN-105](../plans/PLAN-105-game-object-runtime-model.md) 与
[ADR-110](../decisions/ADR-110-game-object-runtime-model.md)。

## 变更

删除 scene_instances::capture_description/serialize 及内部 serialize 实现。
运行实例实现不再调用 asset_file 编码文件；旧重载仍需的临时描述构造收为私有
capture_reload_baseline，仅供该旧实现比较，不提供新的公开替代接口。

运行测试直接检查实体身份、组件数据、资源引用和父关系，不通过序列化 World 验证运行修改。
自定义组件文件往返明确使用调用方的描述，并按已知对象采集组件。
Prefab 测试检查有效变换与源描述分离，刷新继续使用调用方保留的源描述。

属性检查器示例保留激活回执中的原始场景描述，只显式写回本示例修改的相机组件和变换，
再调用 asset_file 编码。保存与重载演示保留，不恢复运行场景整体保存接口。

## 验证

Windows Clang Debug 默认构建通过，随后 84 项 Scene/Prefab/World/组件/Editor/IPC
及边界回归全部通过（85.79 秒）；属性检查器示例单独执行并通过，验证修改、保存、重载闭环。
日志：`build/plan105-runtime-capture-build.log`、
`build/plan105-runtime-capture-tests.log`、`build/plan105-runtime-capture-example.log`。
修改行已格式化，`git diff --check` 通过；源码、示例和测试无旧公开调用残留。

## 尚未完成

旧手动 instantiate_prepared、旧来源重载、私有比较基线及 persistent 分类仍存在。
本次删除的是对外作者捕获/保存能力，不等于整个运行来源状态迁移完成。
持久父关系的对象地址迁移仍待实施。未执行 GPU 大场景、远端 CI 或远端操作。
