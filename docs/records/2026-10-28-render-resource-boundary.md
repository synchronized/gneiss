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

## 后续职责纠正：资源工厂由 Application 拥有

前述迁移完成了目录和接口收口，但 Render 当时仍拥有 resource_bindings，并通过它更新上层对象。
本次删除该对象，改为 Application 显式拥有 render_resource_factory；Render 不包含、持有或调用工厂。
Loader 主动提交 CPU 内容，成功后由工厂更新资源元信息和显式传入的材质纹理依赖。
销毁队列随工厂关闭，Render 只负责 RID 内容及后端。ImGui 只查询已有纹理引用。
迁移入口仍允许调用方显式接管既有 RID，但材质只能绑定已有纹理引用，不递归创建拥有者。

Windows Clang Debug 全量编译通过，上述 18 项相关测试再次全部通过。
新增验证覆盖 Render 不反向修改上层元信息、缺失/错误材质依赖拒绝且不修改内容、
裸纹理查询不取得所有权、先建立纹理拥有者后材质可重试、工厂关闭不关闭 Render、
重复关闭及跨线程晚释放。Core 负例检查阻止 Render 重新依赖工厂、队列和私有访问器。
改动行格式与 git diff --check 通过。未运行大场景、GPU 图像回归、远端 CI 或跨平台矩阵。

## 后续收紧：删除外部 RID 接管

删除 reference_mesh/texture/material 的成员及自由函数入口；工厂只为自身创建的新 RID 构造拥有者。
Loader 通过 reserve_* 直接取得强引用，程序化资源通过 create_* 创建；材质显式传入纹理强引用。
find_* 仅锁定本工厂已有控制块，裸 RID 和其他工厂的 RID 均不能变成新的拥有者。
队列删除器、创建失败清理及工厂关闭只销毁仍有效的记录，避免重复提交销毁请求。

Windows Clang Debug 全量编译及 18 项相关测试通过，改动行格式检查和 git diff --check 通过。
新增检查覆盖旧接管接口不可调用、资源不可由 RID 直接构造、强引用复制后仅最后释放才回收、
两个工厂共用 Render 时身份与关闭隔离、跨工厂材质依赖拒绝。底层裸 RID 创建/销毁接口仍保留，
上层资源提取的 RID 仅借用，不允许混用其销毁权。
未运行大场景、GPU 图像验证、跨平台构建或远端 CI。

## 工厂迁入功能层

将 render_resource_factory.hpp/.cpp 移到 engine/function/resource，更新全部包含路径、CMake 源清单
和原生接口头清单。删除 Resource 反向依赖功能层的工厂例外；私有访问器授权随工厂路径迁移。
工厂接口和实现除包含路径外保持一致，仍由 Application 独立持有，不并入 render_service。

Windows Clang Debug 全量编译通过；Core 正反边界、World 边界、内部头可见性、Render Loader 与
UI 绘制列表共 6 项测试通过。改动行格式检查和 git diff --check 通过。
未运行大场景、GPU 图像验证、跨平台矩阵或远端 CI。
