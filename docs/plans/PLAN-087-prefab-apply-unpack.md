<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->
# PLAN-087：统一 Prefab Apply 与 Unpack

状态：已完成，2026-10-30。延续 ADR-097。

1. 移除 Apply/Unpack 手工解释 Transform JSON 的分支；复用类型化编解码和 `prepare_prefab_reference`。
2. Unpack 物化覆盖后的组件和层级，映射新 UUID，保留实例根变换/名称/父关系及自有子对象，接入既有快照撤销和保存。
3. Apply 生成候选来源，清空当前实例已应用的覆盖，预检查当前编辑场景全部同源实例；冲突返回实例、来源对象、组件及属性路径，任何文件写入前拒绝。

不引入跨文件事务、磁盘撤销或项目全局扫描。来源保存成功仍是历史边界；其他场景在下次加载时检查冲突。
类型化保存与 Scene/Prefab codec 一致：忽略的未知 JSON 字段不会被保留。
验证使用 Debug 编译和相关 CPU 回归，不运行 GPU、大场景、浏览器和远端 CI。

## 验收结果

- Apply/Unpack 复用类型化 codec 与统一覆盖入口，删除旧的 Transform JSON 专用处理。
- Inspector 根据实例全部覆盖判断 Apply 可用性，根节点和仅有组件覆盖的实例均可操作，并显示冲突路径。
- 新增 `gneiss.editor.prefab-author-operations`：验证组件新增、移除、嵌套属性、同源实例冲突、解包层级与自有子对象、快照撤销/重做和保存重载。
- 更新旧作者操作测试的类型化保存预期；修正相机 GUID 和 Lantern 测试中实例根重复计数的旧预期。
- `cmake --build --preset windows-clang-debug -j6` 通过；35 项相关 Scene/Prefab/Editor CPU 测试全部通过。
- 未运行 GUI 手工操作、GPU、大场景、浏览器和远端 CI。
