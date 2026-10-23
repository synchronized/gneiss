<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 0.51：场景对象职责与加载协调收敛

## 范围与验证方式

用户已批准继续对象职责、加载链路和编辑依赖收敛。分组实施，不把资源协议删除等同于整体架构完成。
按用户最新要求，改造期间只编译并做源码、格式和差异检查；不执行测试、大场景或耗时 CI。
行为验证集中记录，留到整体架构完成后统一执行。沿现有特性分支本地提交，不执行远端操作。

## 所有权审计

| 对象 | 实际所有者与内容 | 保留理由 / 可清理内容 |
| --- | --- | --- |
| scene_description | scene_instance 持有作者描述 | 序列化和结构编辑的作者值来源 |
| scene_instance | scene_instance_service 管理实例；实例持有节点、实体和资源引用 | 描述与运行时记录中的名称重复，可删除后者；UUID 仍用于构建中节点解析，先保留 |
| application_scene_state | scene_session / 场景加载任务持有 World 与实例服务 | 一个域可包含多个 Scene；负责先销毁实例再销毁 World，并非空壳 |
| scene_session | Application 持有；管理活动域、退休域、结构加载及视觉队列 | 场景切换协调与单实例节点职责不同，保留 |
| scene_load_service | session 持有；准备 Scene/Prefab 结构、预留引用、构建新域 | 不再需要拥有或创建 GPU 上传服务 |
| texture_load_service | session 持有；视觉请求与上传回执 | 改为 session 生命周期内复用，切换前排空旧请求，不随每个新域重建 |
| scene_document | Editor 宿主持有；借用 Application，拥有 Scene 句柄的卸载责任、选择、脏状态和查询镜像 | World 句柄用于拒绝访问旧域，不是重复所有权；镜像被 UI 和命令消费，不能直接删除 |
| editor_command_history | Editor 宿主持有，文档切换时清理 | 保存作者命令，不下沉到 Runtime |

## 当前调用链与第一批删除清单

宿主 → Application → scene_session → scene_load_service → scene_instance_service::instantiate_prepared。
结构 ready 后 session 交换活动域；视觉队列经 session 持有的 texture_load_service 独立推进。
Editor document 借用 Application 的实例服务查询和编辑节点，不拥有第二个 World。

第一批：删除场景请求里的视觉服务、后端回调副本、服务转交参数和重建计时项；
删除普通 Scene 运行时对象的名称副本及重命名双写。决策见
[ADR-073](../decisions/ADR-073-scene-responsibility-ownership.md)。

## 后续顺序

1. 第一批完成后继续检查描述与运行投影的重复数据，区分作者状态、ECS 状态和查询镜像。
2. 检查加载服务命名、同步/异步重复处理和纯转发层；不再增加 manager 或第二套请求状态。
3. 检查历史和反射的实际消费者，把编辑专属状态保留在 Editor；不直接删必要的结构提交保护。
4. 职责稳定后统一文件布局、命名和说明，再执行集中行为验证。

## 待统一验证

- 同步/异步场景创建、重命名、查询、序列化、撤销/重做和结构重载名称一致。
- 重复切换复用视觉服务；请求号不因场景切换重置，旧回执不能进入新场景队列。
- 切换时旧上传取消并排空，失败保留已成功资源，关闭不悬空、不泄漏。
- 世界切换后旧文档仍被域身份检查拒绝；UI 镜像和命令历史保持正确。

当前状态：第一批已实现，`cmake --build --preset windows-clang-debug -j4` 编译通过。
本批未运行测试、大场景或 CI；上述行为留待统一验证，后续职责审计尚未完成。

## 第二批：删除跨帧场景实例化

- 删除独立构建器及其跨调用状态，实例服务一次同步消费准备结果。
- 保留后台读取、解析、来源校验和视觉资源逐项加载；结构实例化不等待 GPU 上传。
- 现有构建测试改为同步创建、无额外文件读取、失败清理和旧场景不受影响的检查。
- 实例化开始后不支持中途取消；大场景可能阻塞所属线程，此限制已由用户接受。
- Windows Clang Debug 开发构建通过；现有测试已适配并编译，未执行测试、大场景或 CI。
  同步创建、无额外 I/O、失败清理及切换行为留待架构统一验证。
