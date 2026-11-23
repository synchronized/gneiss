<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Runtime/Editor 边界与值返回收尾记录

## 范围与结果

2026-11-09，在 feature/0.52-architecture-cleanup 完成 DEV-052 本次三项收尾。
本记录不表示整个版本完成或发布；剩余清单见 [DEV-052](../plans/DEV-052-architecture-cleanup.md#收尾审计后的明确待办)。

## 代码调整

Application::create 返回 result<shared_ptr<application>>，request_scene 返回 result<uint64_t>，
activate_scene 返回 result<scene_load_completion>。删除旧输出参数签名，迁移 Runtime、Editor、示例及测试；
保留创建失败清理、线程约束、场景激活互斥与原有错误语义。

resource_load_group::advance 只更新批次观察状态，不再调用共享 asset_manager::advance。
Application::poll_prepared_assets 显式推进资源 I/O，其他宿主入口沿用已有推进责任。
删除 asset_executor_attached_，从已有加载组拥有者判断装配状态。
这表示集中推进责任，不保证每帧只有一次 advance 调用。

## 所有权审计

| 代码位置 | 核查结果与处理 |
| --- | --- |
| src/runtime/application.cpp | Runtime/Editor 宿主使用 Application 拥有的资产管理器；未发现另一个独立资产缓存 |
| src/runtime/loading/resource_load_group.hpp | 保存请求与完成观察结果，保留批次状态；删除隐式推进管理器行为 |
| src/runtime/loading/scene_service.cpp | 场景活动域、退休域和完成回执用于场景生命周期；清理边界 collect_unused 保留，不是第二条加载管线 |
| src/editor/documents | 文档、历史与投影持有编辑数据及必要资源引用；不能作为重复资产缓存删除 |
| src/runtime/application.cpp 的关闭路径 | 正式异步关闭保留；同步 shutdown/stop 兜底仍存在，列入后续约束审计 |
| Application 的环境数据路径 | 保留 file_service 到 render 的专用读取，不宣称所有内容已统一为五类资产 |

边界检查增加批次不得调用共享管理器 advance、collect_unused、stop、request_stop 的规则，
负例测试逐个注入上述调用并核查拒绝，最后核查清理后的正例。

## 验证

- Windows Clang Debug 默认构建通过，日志 build/dev052-cleanup-build.log。
- 43 项 CPU 测试全部通过，日志 build/dev052-cleanup-tests.log。
- 覆盖原生 Application 创建与流程、日志、场景加载与生命周期、Prefab、资产加载、编辑器场景命令、
  文档历史及内部头与核心边界检查；创建测试直接验证 result 的成功拥有值与失败错误。
- 未执行窗口/GPU、像素、大场景、基准、跨平台完整矩阵或远端 CI。
- 未推送、创建 PR、合并或发布；本批仅做本地提交。

## 保留限制

Application 的资源请求与部分轮询仍使用输出参数，World 等接口的值返回审计尚未完成。
本批没有改变流送能力、后端创建条件或文档历史语义，也没有新增缓存或协调服务。
