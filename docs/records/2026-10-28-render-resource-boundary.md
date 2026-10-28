<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Render 资源接口收口验收

## 范围

对应 [PLAN-054](../plans/PLAN-054-render-resource-model.md) 的 2026-10-28 追加范围。
当前契约以 [Render Reference](../reference/render.md) 为准；不覆盖计划中另行启动的 CPU 驱逐、
Mip/LOD 流送或渲染性能扩展阶段，不回写此前 Sponza 与完整测试矩阵的历史结果。

## 实施结果

- 删除 `render_resource_service` 和 `resources()` 表借用入口。Application、Loader、帧构建与 UI
  使用 `render_service` 的 RID 接口；CPU 数据表只保存内容，不保存上层资源对象。
- 将原有弱身份关联、元信息更新、材质强依赖和销毁队列迁入资源层 `resource_bindings`。
  三类资源共用原有强引用控制块与销毁端点，没有新增 URI 缓存、类型擦除基类或 GPU 进度协议。
- 纹理像素视图与销毁队列移到资源目录。程序化纹理强引用通过资源层函数创建。
- 服务构造和 CPU 资源操作不调用 GPU 初始化；未初始化 GPU 时 shutdown 成功并关闭资源，重复关闭成功。
- 私有存储头和身份关联头被限制在装配实现中；宿主可以使用服务接口但不能获取私有表。

## 本地验证

环境：Windows、Clang、Debug、共享库，使用 `windows-clang-debug` preset。

- 完整编译通过，包含 Engine、Runtime、Editor、示例及测试目标；未关闭警告。
- 18 项相关测试全部通过：资源销毁队列、原生渲染生命周期、RID 资源表、材质资源、UI 绘制列表、
  帧包、渲染快照、场景加载、资产准备与 Loader、纹理加载服务与 Application、Prefab 实例、
  场景实例化、Core 正反边界、World 边界、内部头文件可见性。
- 新增/保留验证：工作线程最后释放后不能复活同一 RID 的第二个拥有者；所属线程级联释放材质纹理；
  未启动 GPU 的关闭统计为零；重复关闭及关闭后工作线程晚释放安全。
- 头文件边界验证同时检查旧 `resources()` 入口未恢复，以及私有表和关联实现头不可见。
- 改动行格式检查及 `git diff --check` 通过。

未运行大场景、GPU 图像回归、静态库矩阵、跨平台构建或远端 CI；未修改 Granit。
GPU 上传实现保持原有路径，本次验证不代表真实设备丢失和驱动上传故障已经覆盖。
