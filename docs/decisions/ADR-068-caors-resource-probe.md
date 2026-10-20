<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-068：CAORS 单资源接入验证边界

## 状态与背景

2026-10-20，采用，范围仅限独立验证目标。用户要求先接通一个真实 CPU 资源，
验证 CAORS 资源管理与现有 libuv、调度器的组合，完整 VFS 迁移仍按
[C51-07](../plans/UPSTREAM-051-caors-async-vfs.md)推进。

## 决策

- 新增默认关闭的 `GNEISS_BUILD_CAORS_PROBE`，CAORS 0.4.0 固定提交，MIT 许可。
- CAORS 适配位于 `src/engine/platform/caors`，以内部静态库 `gneiss_caors_adapter` 编译，
  目前仅验证 EXE 链接；不链接 Engine DLL，不向 SDK 传播其头文件或对象。
  不影响普通 Gneiss 安装包；完整引擎集成的动态/静态部署另行确认。
- 唯一资源缓存由 CAORS cache 提供，交给 asset_manager 注册管理；不再创建平行缓存。
- 用 Gneiss task_executor 的独立 scope 适配 external_executor；主线程 completion 队列不创建线程。
- 首个资源使用既有 `parse_action_map` 解析为强类型 action_map，校验键值和绑定规则；
  解析器与旧 VFS 的同步加载函数分离，共享相同实现；资源就绪后仅由宿主显式 poll 应用到 InputService。
- CAORS provider/路由快照负责来源解析；本阶段明确仅使用 root 为空的 native 后端，
  target_prefix 解析为本地路径。打开通过 CAORS request_open / VFS start_open，读取通过 async_file，
  libuv 回执封装于验证后端，宿主 pump 回执，
  不阻塞工作线程等待文件，也不另造 URI 路由表。

## 限制与后续

打开、范围读取与显式关闭已有验证适配，尚未统一生产 VFS。
libuv 显式关闭拒绝新读并等待在途读结束，实际关闭回调结束后发布结果，重复关闭共用终态。
验证适配需要宿主继续 pump；放弃推进前调用 abandon 解除完成凭证保活环，再排空 loop。
实验有 1 MiB 文档限额，不是纹理流式方案。取消先中止逻辑加载并放弃回执，
不能声称强制中断了已提交的操作系统 I/O。
异常退出也必须先关闭工作 scope、放弃回执并排空 loop，再销毁 Loader/manager。

平台适配以 `.cpp` 隐藏实现，仅内部头文件暴露宿主 pump/abandon/collect 与工厂；
平台模块不依赖输入、场景或其他业务资源。输入资源 Loader 与待应用对象位于 function/input，
由可选私有目标 gneiss_caors_input 构建，当前仅独立宿主链接。
待应用对象只保存 CAORS 请求订阅，不建缓存，不由后台持有 InputService；新请求取消旧订阅，
仅最后请求可生效。成功应用一次，失败和取消保留现有映射及 action_id。
下一步接入 application 的宿主生命周期；现有同步公共 API 与 Editor 启动路径尚未切换。
替代方案是一次替换所有资产路径，风险更大，且无法定位协议与生产迁移的问题边界。
