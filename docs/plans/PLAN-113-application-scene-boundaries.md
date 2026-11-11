<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-113：应用装配与场景模块职责重整

## 状态

2026-11-11：实施中。第一、二组编辑职责、Transform 标识及资产批次迁移完成，Windows 全量构建、
7 项定向回归与分层检查通过；第三组 World 直接所有权及同 World 激活已落地，旧场景请求协调与应用目录迁移尚未完成。
结果见[第一组记录](../records/2026-11-11-plan113-editor-boundary.md)和
[资产批次记录](../records/2026-11-11-plan113-asset-batch.md)。
承接 VER-052；长期决策见 [ADR-117](../decisions/ADR-117-application-scene-boundaries.md)。
本计划的目标目录、迁移顺序和验收清单以本页为权威来源。

## 背景与目标

当前 application 私有存储拥有 scene_service，而后者拥有 active_/retired_ World，
同时处理加载、来源重载和视觉资源。resource_load_group 汇总渲染资产请求，但携带
session/revision、批次数量限制和多套准备入口。scene 目录又混合运行回放与编辑辅助。

目标是 Application 管装配，AssetManager 管资产，World 管运行实例，EditorDocument
管编辑内容。减少重复状态和间接所有者，实际调用链与文件布局同步迁移。

## 非目标

- 不重写 ECS、反射、序列化、资产缓存、文件服务或编辑历史。
- 不接入流送、脚本、天空盒新协议、跨 World 移动或通用事务。
- 不以改名替代职责拆分，不保留长期兼容转发层。
- 不触发远端 CI、推送或发布；本计划不新增发布授权。

当前第三组进展见 [World 所有权迁移记录](../records/2026-11-11-plan113-world-owner.md)。

## 目标对象与所有权

```text
Application
  file_service / task_scheduler / asset_manager / render_service / input_service
  world
    ECS 对象与组件
    scene_instance 槽位
EditorSession
  scene_document / edit_history
  scene_projection → 借用 World
```

图表示职责，不要求修改现有注入执行器的所有权。注入对象必须存活至依赖工作关闭结束。
World 直接由 Application 持有，普通场景切换不替换 World。编辑器预览可独立拥有 World。
场景追加与卸载通过 scene_instance 表达；不重新引入对象列表与 ECS 的重复拥有关系。

## 目标流程与 API 边界

以下是概念流程，不是已存在的可编译 API：

```text
assets.load<scene>(uri)
  → 异步取得场景资产和必需结构依赖
  → 所属线程取得完整结构内容
  → instantiate(destination_scene, content)
  → 视觉依赖独立完成，缺失项保留诊断和占位行为
```

- 优先复用现有 instantiate(scene_instance, composed_scene, bindings, parent) 的追加语义，
  不为演示代码再增加 instantiate(world, asset) 包装层。
- composition 纯内存展开 Prefab；所需结构数据必须由资产加载完成条件保证。
- scene_update 复用当前 apply_scene 的身份保持语义；来源关联与基线由调用方持有。
- 加载失败不修改 World；实例化失败清理本次新建对象，既有对象不受影响。
- 普通替换由宿主在新实例建立后卸载旧实例；不保证视觉资源全部成功，不默认保证全操作回滚。
- 来源热重载保留为显式宿主操作，编辑器走文档/投影；不由运行对象存储 Prefab 来源或覆盖。
- 完成通知经现有队列在所属线程消费；查询不推进，禁止阻塞主线程，不恢复逐请求轮询。
- 批次优先组合既有 asset_request；确有多个消费者才提炼共享批次操作，复用统一资产记录。
  session/revision 留在调用方；加载状态不再用 applied 暗示编辑操作或 GPU 上传完成。

## 目标目录

```text
src/
  application/
    application.hpp/.cpp
    application_configuration.hpp
    detail/main_loop_wakeup.hpp
  engine/
    resource/
      asset.hpp / texture.hpp / mesh.hpp / material.hpp / scene.hpp / prefab.hpp
      scene/                    # 描述、地址和覆盖数据
      loaders/
    function/
      asset/
        asset_manager.hpp/.cpp
        asset_request.hpp
        asset_loader.hpp
        detail/                 # 加载操作；必要时包含批次汇总
      scene/
        world.hpp/.cpp
        game_object.hpp/.cpp
        scene_instance.hpp/.cpp
        components.hpp
        component_registration.hpp/.cpp
        composition.hpp/.cpp
        instantiation.hpp/.cpp
        scene_update.hpp/.cpp
        detail/
          world_state.hpp
          hierarchy.hpp/.cpp
          component_storage.hpp
          prefab_replay.hpp/.cpp
      render/
        render_service.hpp/.cpp
        render_extraction.hpp/.cpp
        detail/
  editor/documents/
    scene_document.* / scene_edit_session.* / scene_projection.*
    edit_history.* / prefab_edit.*
  host/
    runtime/
    editor/
```

这是目标职责布局，不要求将已有等价文件机械拆成列出的每个名字。
Application 可暂时保留 gneiss::runtime 命名空间，目录迁移不强制无意义的全局符号改名。
Prefab 回放若是编辑器仍需直接调用的契约，应保留公开内部头，不能仅为减少文件移入 detail。

## 实施顺序

### 1. 迁出编辑专用职责

- [x] 将 scene_mesh_binding 移入 editor/documents，清理 Application 无用包含。
- [x] 拆出 prefab_replay/property_override 中编辑候选生成；引擎保留运行覆盖应用。
- [x] 核实 component_identity 的共享 Transform 标识归属，避免新建重复常量。

### 2. 收敛资产批次

- [x] 盘点 resource_load_group 各调用方，统一准备/提交入口并移除调用方版本字段。
- [x] 用既有请求组合承接批次；必要共享操作落到 function/asset，禁止新增缓存。
- [x] 更新编辑命令、取消、部分失败和结果消费；完成项进入队列，不逐帧扫描未完成请求。

### 3. 迁移 World 所有权与场景调用链

- [x] Application 直接拥有 World，保持实例句柄与组件资源生命周期规则。
- [x] 宿主使用资产请求、组合和实例化，支持向同一 World 追加场景。
  - [x] Runtime 打开场景直接请求资产并组合、实例化；不再调用 scene_service::open/activate。
  - [x] 编辑器直接加载、组合和实例化，以文档快照及对象绑定接入编辑会话。
- [x] 删除共享候选/退休 World 状态与专用退休统计。
- [x] 替换/卸载策略回到宿主。
- [x] 来源重载状态交给实际调用方；编辑器统一使用文档投影，不复制来源权威数据。
- [ ] 删除 scene_service 和旧 open/activate/reload 协议，迁移全部消费者，无长期并行入口。
  - [x] Runtime 来源重载直接持有资产请求，调用既有场景更新算法，不再调用 scenes().reload。
  - [x] 四个场景示例改用资产请求及实例化回执，移除 examples 中的旧服务调用。
  - [ ] 迁移剩余测试及完成回执类型，删除旧服务实现。

### 4. 整理算法与目录

- [x] scene_application 改为 scene_update，保持输入输出和失败语义明确。
- [x] render_extraction 移到渲染前端；边界检查区分前端桥接和后端实现。
- [x] Application 装配迁入 application；配置与主类相邻，主循环等待放 detail。
- [ ] 删除 runtime/loading 的迁移期协调代码，清空旧 runtime 目录。
- [ ] 同步 CMake、内部接口清单、头文件依赖、示例及测试；删除废弃头和函数。

### 5. 验收与文档收尾

- [ ] 完成下列行为与边界验收，更新 Reference/Concept 的当前事实。
- [ ] 记录迁移差异、未验证平台和剩余风险，更新版本计划状态。

## 测试与验收

- 开发阶段先完成受影响目标编译；结构统一后集中做小型定向 CPU 回归，不运行大场景和耗时 CI。
- 同 World 追加两个场景，卸载其一不影响另一场景；切换后 World 地址保持稳定。
- 空场景、缺失结构依赖、视觉依赖失败、实例化部分失败均有明确结果，无对象或资源泄漏。
- 取消/替换请求、迟到通知、关闭中的回调安全；宿主失效后请求不访问悬空所有者。
- 重载和编辑投影保持未删除对象身份；删除后旧句柄失效，撤销重做行为无意外回退。
- 批次支持部分成功、取消与独立观察者；无编辑 session/revision 或重复资产缓存。
- 核查 engine 不包含 application/editor/host；渲染后端不依赖 World/ECS。
- git diff --check、文档链接、相关编译与边界检查通过；未执行的平台矩阵明确记录。

## 风险与未决事项

主要风险是消费者依赖旧 World 替换、场景退休统计或隐式来源重载；先列出并迁移实际调用方，
不得简单删除行为。持久 World 下全局相机选择、投影绑定和场景卸载清理需重点核对。
暂无阻塞实施的产品决策；具体类型签名和是否保留共享批次以调用方证据决定，不能扩成新服务层。
