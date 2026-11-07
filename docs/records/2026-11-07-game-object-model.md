<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Game Object 模型实施记录

日期：2026-11-07。关联 [PLAN-105](../plans/PLAN-105-game-object-runtime-model.md)。

## 阶段 1：命名与对象描述入口

- object_description 改为 game_object_description，同步注册、校验、场景组合、导入器、
  编辑器、测试和当前参考文档；没有旧名别名。
- 新增 resource/scene/game_object_description.hpp，集中对象及相关组件/Prefab 描述；
  scene_description.hpp 保留场景文档外壳与诊断。Scene 和 Prefab 共用对象描述。
- 保留 optional prefab 与 components 的既有互斥校验，不引入 scene_entry variant。
  类型 GUID、属性 ID 和 JSON 字段保持原值。
- 新头加入目标源清单与内部接口白名单；首次构建发现白名单遗漏，补齐后默认构建通过。
- 34 项场景、Prefab、组件描述、默认注册和内部头可见性回归全部通过；
  core/world/reflection 三项边界检查通过。
- core-boundary-negative 首次因本轮 30 秒超时中断，重跑受到残留探针污染；
  使用新的构建临时目录运行同一脚本通过，随后清空本轮残留的生成探针。
  未修改检查规则、未屏蔽失败。
- git diff --check 通过；未运行 GPU 大场景、远端 CI 或跨平台矩阵，未推送。

## 尚未实施

组件 UUID 仍由 component_entry 持有。当前 Meta 会拒绝带属性的基类，先补继承属性访问、
捕获/恢复和编解码，再迁移 UUID，不在每个组件上重复注册该字段。
运行时仍使用既有 Scene Tree/ECS 与 prefab_binding；上述重命名不代表运行存储已统一。
其余范围继续以 PLAN-105 检查项为准。
