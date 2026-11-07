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

## 阶段 2：组件 UUID 归属迁移

- UUID 移入 component_description_base；删除 component_entry 和专用迭代器，
  component_descriptions 直接保存不可变描述引用，并校验 UUID 非空、实例/类型不重复。
- 基类字段只注册一次，保留属性编号 0xffffffff，标记 editor_read_only。
  文件直接保存多态引用，UUID 位于 $content 中；不读取旧 uuid/description 包装。
- 有类型 set 在冻结引用前保留目标身份；类型擦除引用通过已注册编解码复制后调整身份，
  不修改外部共享描述，相同身份直接共享。自定义 codec 负责保存/恢复基类 UUID。
- 场景捕获、Prefab 新增/编辑、文档恢复和子树复制同步身份；复制后重新捕获验证
  组件内部 UUID 与领域快照索引一致。运行时身份表暂时保留，随阶段 4 统一存储继续清理。
- 导入器仍使用确定性 UUID；迁移样例、文件夹具和相关诊断路径断言。
- 首轮构建暴露运行实例和导入器的旧包装调用，已全部修复；首轮 42 项回归中
  三项因属性数量与空引用诊断路径变化失败，更新对应精确断言后重新验收。
- 最终 Windows Clang Debug 默认构建通过；45 项定向回归全部通过，包含导入器、
  Scene/Prefab、组件身份/生命周期、共享引用隔离、文档快照和撤销恢复。
  git diff --check 通过。未运行 GPU 大场景、远端 CI 或跨平台矩阵，未推送。

## 阶段 3：运行编辑与覆盖生成分离

- 删除 scene_instance / scene_instances 的组件覆盖捕获与应用接口；覆盖历史入口留在编辑文档。
- 组件增删、替换及网格资源绑定不再生成 Prefab 覆盖；统一 set_node_transform 只更新 World。
- 文档接受覆盖候选后下发有效组件；组件身份变化先移除再添加，网格历史继续恢复实际资源引用。
- 回归验证运行变换立即生效、加载时覆盖不变，以及重新实例化恢复文档值。
  组件编辑测试改为从文档序列化检查持久覆盖，同时保留运行组件值和撤销断言。
- Windows Clang Debug 默认构建通过。45 项定向回归首轮 44 项通过，剩余组件编辑测试
  因仍从运行实例读取文档覆盖失败；迁移到文档保存入口后单独重跑通过。
  git diff --check 通过；未运行 GPU 大场景、远端 CI 或跨平台矩阵。

## 阶段 3：统一文档组合查询

- 增加 compose_scene_document，输入冻结文档与来源，输出有效对象、父地址、来源信息和冲突。
  同 URI 来源在一次组合内仅恢复一次，不读取文件、不创建 World，不缓存可编辑结果。
- document_object_address 区分普通对象与实例内来源对象；有效组件保持原实例 UUID。
- 编辑目标定位和层级快照改用同一组合入口，删除各自的 Prefab 回放与根值覆盖逻辑。
  节点句柄仍通过运行实例的身份映射匹配，独立投影关联尚未迁移。
- 纯内存回归覆盖同源实例隔离、跨实例根父关系、普通对象挂到实例根、根值覆盖、
  组件身份保留、失效覆盖诊断和来源缺失。Windows Clang Debug 默认构建通过，
  39 项场景、Prefab、组件、编辑查询及 Gizmo 回归全部通过，git diff --check 通过。
  未运行 GPU 大场景、远端 CI 或跨平台矩阵，未推送。

## 尚未实施

运行时仍使用既有 Scene Tree/ECS 与 prefab_binding，保留加载时的来源映射、覆盖及冲突记录。
阶段 3 尚需迁移来源查询、捕获及刷新；阶段 4 再统一运行存储。
其余范围继续以 PLAN-105 检查项为准。
