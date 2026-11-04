<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：场景生命周期协调首批迁移

2026-10-15，Windows Clang Debug 本地验证。实施依据为
[ADR-063](../decisions/ADR-063-scene-lifecycle-coordination.md)，不表示 0.51 完整验收。

## 实际变更

`src/engine/function/application/scene_session.hpp/.cpp` 现在唯一拥有活动场景域、待退役域、
场景加载服务、活动域纹理服务和退役统计。Application 通过该组件初始化、推进、激活和关闭，
不再保留同一组状态。初始化和任务装配异常仍由 Application 的错误边界转换。
任务装配在两个服务都构造成功后发布，避免半装配状态。

libuv 执行器创建、VFS 和 Granit 上传回调接线仍在 Application；session 接收现有接口。
scene_load_service 继续承担候选准备，场景激活仍由宿主显式请求。关闭先清理加载服务、纹理服务、
待退役域和活动域，然后 Application 完成其余系统关闭。无新增公开 SDK 或动态库。

新增边界检查拒绝 session 直接包含 platform、Granit 或 render/backend；三个禁止包含反例与
一个允许依赖上传服务的正例均通过。该检查是首批约束，不代表整个 application 私有头已隔离。

## 生命周期核对

`render_frame_packet` 的资源快照共享持有 mesh/material/texture；执行器将已执行 packet
移交 completion，不能把执行回调返回误认为所有 CPU 数据已经释放。
`granit_render_service::shutdown` 排空执行器、收集完成结果，提交 GPU 关闭命令并等待，
停止执行器后清空复用 packet。Application 仍在该过程完成之后停止资产 I/O 执行器。
本次未改变渲染后端实现，也未实测完整 GPU 场景退出。

## 本地验证

- 重构前：相关加载与边界测试 7/7 通过。
- 重构后：构建 Engine、场景加载 Application 测试、Editor 加载测试、帧快照与执行器测试成功。
- 定向 CTest 9/9 通过：editor.scene-loader、editor.runtime-scene-loading、render_executor、
  render_frame_packet、scene-load-application、scene-load-preparation、scene-load-builder、
  core-boundary、core-boundary-negative。
- 场景 Application 测试覆盖线程池与协作模式；新增断言验证旧域退役前仍有效且拒绝下一请求，
  以及带在途请求退出后活动 World 句柄失效。
- 所改 C++ 文件 clang-format 检查通过；scene_session.cpp 的 clang-tidy 检查通过。
- `git diff --check` 通过。

测试沿用既有本地配置；未把本地测试耗时解释成完整 Sponza 性能数据。

## 第二批：场景加载操作封装

场景请求、进度查询、完成消费与取消已迁入 `scene_session` 的操作接口。
Application 和宿主适配不再返回或取得 `scene_load_service*`；请求与活动域的互斥判断由
session 执行，宿主适配保留句柄/线程校验及异常到结果码的转换。激活仍是独立操作。

新增未装配执行器的回归：请求返回 NOT_READY 并清零请求 ID，进度查询成功但不活动，
轮询返回 NOT_READY 且 ready 为 false，取消返回 NOT_READY。在线程池和协作模式下验证。
本批 Windows Clang Debug 构建成功，原有 9 项定向 CTest 全部通过，所改文件格式检查通过。
scene_session.cpp 的 clang-tidy 无诊断；application_host.cpp 有 17 条诊断，涉及已有接口的
相邻同类型参数、无花括号单语句及纹理服务指针到 bool 的转换。相关代码未在本批改变，未抑制
诊断，不将宿主文件静态检查描述为无告警。`git diff --check` 通过。

## 后续验收范围

完整模块清单、宿主内部接口与私有头可见性仍需收敛。目前宿主适配仍能借用纹理加载服务指针，
纹理服务借用不能跨场景激活缓存。尚未完成共享/静态全矩阵、Linux、安装消费者和完整 Sponza
重构前后性能对照；对照方法已在实现前写入 DEV-051。未触发 Actions、推送或发布。
