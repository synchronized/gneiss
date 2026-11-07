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

## 阶段 2：继承属性基础能力

- Registry 在冻结时合并完整基类链的属性，记录 declaring_type；拒绝 ID 与名称冲突。
  先建立候选属性表，校验通过后提交，缺失基类可以补注册后重新冻结。
- instance 访问与直接 read_property/write_property 按 upcast 链调整声明类型地址，
  标量、容器、嵌套对象和受约束 setter 共用现有访问与序列化路径。
- 增加两层继承、非零偏移基类、属性冲突、失败后重试、容器修改、setter 校验、
  JSON/value 往返和多态引用恢复的回归用例。
- Windows Clang Debug 默认构建通过；Meta、序列化、Scene/Prefab、组件描述及相关编辑器
  共 41 项定向回归通过，git diff --check 通过。未运行 GPU 大场景、远端 CI 或跨平台矩阵。

## 尚未实施

组件 UUID 仍由 component_entry 持有。继承属性基础能力已具备，下一步迁移 UUID 及集合、
文件和编辑恢复路径，不在每个组件上重复注册该字段。
运行时仍使用既有 Scene Tree/ECS 与 prefab_binding；上述重命名不代表运行存储已统一。
其余范围继续以 PLAN-105 检查项为准。
