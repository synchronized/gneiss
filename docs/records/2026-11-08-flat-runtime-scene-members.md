<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 统一运行场景成员与删除旧 Prefab 路径

日期：2026-11-08。对应 [PLAN-105](../plans/PLAN-105-game-object-runtime-model.md)。

## 变更

正式加载、手动实例化和投影重建统一消费 compose_scene/apply_scene。
删除 prefab_binding、scene_instance_prefab、专用创建/移除/重载及私有作者捕获基线，
运行成员没有 persistent 分类，也不持有模板、覆盖表、冲突或来源索引。
来源关系由实例化回执交付给 Runtime 协调方及 Editor 投影。

Runtime 热刷新仅接受正式激活且关联有效的场景；按实际名称、组件及父关系比较目标。
来源未修改的变换保留游戏运行值，来源显式修改时采用新值。
应用不承诺整体回滚；首次创建失败回收全部新成员并恢复原活动相机。

旧绑定测试迁移到纯组合/通用应用及正式 Runtime 激活，覆盖同源多实例、运行 UUID 独立、
重挂接、增删、身份保持、来源释放、失步、错误线程、视觉依赖失败及活动相机恢复。
Editor 往返测试通过加载回执定位来源对象，不按文档 UUID 查运行 Prefab 根。
边界负向测试改为检查通用应用文件；没有关闭测试或降低检查等级。

## 验证

Windows Clang Debug 默认构建通过，84 项相关回归全部通过（86.88 秒）。
修改行格式化及 git diff --check 通过。日志见
`build/plan105-runtime-flat-build.log`、`build/plan105-runtime-flat-tests.log`。
未执行 GPU 大场景、远端 CI、推送或发布。

## 剩余范围

持久父关系仍使用单个 UUID，下一步贯穿 object_address，并完成计划的最终验收。
本记录不代表 PLAN-105 已全部完成。
