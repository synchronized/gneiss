<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Game Object 运行模型最终验收

日期：2026-11-08。对应 [PLAN-105](../plans/PLAN-105-game-object-runtime-model.md)，
边界决策见 [ADR-110](../decisions/ADR-110-game-object-runtime-model.md)。

## 完成内容

World 唯一拥有实体、层级和场景成员；Game Object/Scene Instance 为借用句柄。
组件实例 UUID 属于描述基类；运行组件只接收有效数据。Runtime 与 Editor 使用共享组合/应用，
旧 Prefab 绑定、成员 persistent 分类、专用运行创建/删除/重载与作者捕获已经删除。
来源协调、历史及文档投影分离，具体运行迁移见[上一组记录](2026-11-08-flat-runtime-scene-members.md)。

本组完成持久父地址：game_object_description、冻结记录、局部历史和子树快照使用
object_address。反射直接保存 object/source 两个 GUID；自有对象可挂到具体 Prefab 成员。
创建、重挂接、复制和恢复保留完整地址；解包按来源成员映射转换父地址。
层级面板显示混合子节点，拖放返回完整地址。样例、模板、导入器、性能脚本和测试同步迁移。

文件读取校验显式对象关系，来源加载后组合校验具体成员存在性、完整父链及深度。
失效父成员明确报错，不回退到来源根；复制只改变外层对象身份，来源 UUID 保持。
未发布旧 parent 字符串/null 格式不保留兼容入口；现行文件格式见[参考](../reference/scene-format.md)。

## 验证与边界

Windows Clang Debug 默认构建通过。本组覆盖 86 项相关回归：85 项在整组执行通过，
最后一项 prefab-authoring 的旧 JSON 断言更新后单独复验通过。没有修改或跳过断言以容忍错误。
日志为 build/plan105-parent-address-build.log、plan105-parent-address-tests.log、
plan105-final-build.log、plan105-final-tests.log。上一组 84 项运行/编辑相关回归全部通过。

新增和调整的验证覆盖完整父地址文件往返、来源成员缺失、外层 UUID 复制、源父节点切换历史、
存活实体保持、新建/复制/子树删除恢复、解包父地址转换。既有测试覆盖覆盖冲突、来源刷新、
资源保活、空句柄、跨 World、generation、重复删除、场景卸载及非法层级。
运行模块扫描未发现 Editor 文档依赖、旧 prefab_binding 或私有作者捕获基线；核心边界正负检查通过。
修改行格式检查与 git diff --check 通过。

未执行 GPU 大场景、远端 CI、推送或版本发布。嵌套 Prefab、脚本、流送不是本计划范围。
本记录完成 PLAN-105 验收，不表示 DEV-052 整个版本已经发布。
