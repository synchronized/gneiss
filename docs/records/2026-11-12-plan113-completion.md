<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：应用与场景边界收尾

日期：2026-11-12。代码基线：58d03fde；本轮仅修正文档与汇总验收，不修改运行代码。

## 验收结论

[PLAN-113](../plans/PLAN-113-application-scene-boundaries.md) 的模块迁移及 Windows CPU 验收完成。
Application 唯一拥有 World；Runtime/Editor 分别负责打开、替换和来源重载或文档投影。
旧 scene_service 与 runtime/loading 已删除，原生场景调用传递借用的 scene_instance。

现有用例已覆盖以下边界，本轮没有新增重复测试：

| 行为 | 验证位置（tests 下） |
| --- | --- |
| World 地址稳定、追加场景、单独卸载、共享资产保活、空场景 | scene/scene_load_application_test.cpp |
| 结构依赖失败、取消观察者、固定内容保留 | scene/scene_load_preparation_test.cpp |
| 实例化失败回收候选成员、既有场景保持 | scene/scene_instantiation_test.cpp |
| 跨 World、场景失效、跨线程卸载与子树迁移 | scene/scene_instance_test.cpp |
| 批次领取/取消/移动、独立 Application、在途关闭 | native_application_test.cpp |
| Runtime 来源失败与卸载后重载拒绝 | host/runtime/runtime_scene_loader_test.cpp |
| Scene/Prefab 增量重载与对象身份 | host/runtime/scene_structural_reload_test.cpp、prefab_structural_reload_test.cpp |
| 编辑打开取消/失败保留旧文档、替换与关闭 | host/editor/editor_scene_loader_test.cpp |
| 编辑历史、文档编辑与版本投影 | host/editor/editor_command_history_test.cpp、editor/scene_document_edit_test.cpp、scene_document_versions_test.cpp |

## 本轮验证

Windows Clang Debug 默认构建通过（当前目标已是最新，无需重编译）。17 项定向测试全部通过：

- runtime.scene-loader、runtime.asset-reloader、scene_structural_reload、prefab_structural_reload。
- editor.scene-loader、editor.command-history、editor.scene-document-edit、editor.scene-document-versions。
- native-application-flow、native-application、scene-load-application、scene-load-preparation。
- scene_instance、scene-instantiation。
- core-boundary、core-boundary-negative、internal-header-visibility。

本地输出：build/plan113-final-build.log、build/plan113-final-tests.log，不纳入版本控制。
源码检索确认 Application、Scene、Host、Editor 及根 CMake 无旧 scene_service/runtime/loading 引用。
相关文档相对链接与 git diff --check 通过。

## 差异与边界

- 场景句柄不保活 World；World 销毁后不能调用句柄，包括有效性查询。
- Runtime 重载基线保存 scene_instance，World 从句柄取得，不维护另一份独立 owner。
- 编辑投影正常差异更新保留未删除对象身份；完整重建路径会替换场景句柄，不能承诺所有恢复都保留 ID。
- 视觉加载允许部分失败，不回滚已创建场景；批次没有增加编辑 session/revision 或另一套资产缓存。
- 渲染全量提取已由后续 PLAN-115 的 World 侧同步接管，不恢复 PLAN-113 早期目录草图中的旧提取入口。

未运行 GPU 画面、多平台、静态库完整矩阵、大场景或远端 CI；这些仍属于版本验收。
PLAN-115 渲染背压和 GPU 验收、PLAN-114 测试整理按各自计划继续，不随本计划关闭而标记完成。
