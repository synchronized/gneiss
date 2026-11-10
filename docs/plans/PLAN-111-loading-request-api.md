<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-111：加载请求与主循环推进分离

## 状态与范围

2026-11-10：实施中。推进/查询分离已完成，Windows 全量编译与 16 项定向 CPU 回归通过；
请求对象与旧转发接口替换仍未完成。见[首阶段验收](../records/2026-11-10-plan111-loading-advance.md)。
本计划承接 VER-052 的 Application 加载接口收敛，不改变 World 所有权或资产缓存。
架构边界见 [ADR-116](../decisions/ADR-116-loading-request-api.md)。

## 已确认设计

- 普通资产通过 `application.assets().load<T>(uri)`，沿用 asset_request，不另建缓存或通用任务系统。
- 场景打开是宿主操作：异步取得内容、实例化候选 World；显式激活才切换当前 World。
- 主循环统一推进文件完成、资产服务、批次和场景操作。状态查询与领取回执不执行 I/O、不退休 World。
- 非主循环宿主显式调用 `advance_loading()`；该入口返回推进错误，关闭阶段使用独立关闭驱动。
- scene_open_request 只观察一份操作状态；不得复制一套独立进度状态机。
- 请求不保活 Application。Application 关闭使未完成请求取消；句柄必须能安全识别宿主失效。
- 场景请求采用移动语义；放弃未激活请求取消候选工作。激活成功后请求被消费，重复激活返回失效错误。
- 查询限宿主线程，ready 表示可激活，failed 与 cancelled 独立；不暗示 GPU 上传完成。
- session/revision 属于 Host/IPC 关联信息，不作为普通加载 API 的必填参数。
- 查询状态直接返回值；提交、取消和激活等可失败动作使用 result/error。

## 目标 API（尚未全部实现）

```cpp
auto texture_request = app.assets().load<texture>(uri);
auto scene_request = app.scenes().open(scene_uri);
// run() 统一推进；自定义宿主循环调用 advance_loading()。
if (scene_request && scene_request->ready()) {
  auto activated = app.scenes().activate(std::move(*scene_request));
}
```

`scenes()` 借用宿主场景服务，不新增并列所有者。取消、进度、错误通过请求对象访问。
编辑器批次保留独立请求，作为明确的多资源编辑操作；不把普通单资产加载包装为批次。

## 实施清单

- [x] 统一 advance_loading，移除查询中的推进与旧 advance_scene_load 名称。
- [ ] 引入 scene_open_request，把编号、状态查询和取消收敛到请求；实现宿主失效检查。
- [ ] Scene/Editor/Host 迁移 open/activate；协议关联信息留在 Host。
- [ ] 普通资产调用直接使用 asset_request，编辑批次用独立请求替换 out 参数及 prepare/poll 包装。
- [ ] 删除迁移完毕的 Application 转发接口，更新参考、示例和测试。

## 验收

- 重复查询不推进阶段、不领取别的请求、不回收场景；只调用 run 也能完成加载。
- 协作/工作线程执行模式均验证成功、失败、取消与场景显式激活。
- 请求放弃、宿主关闭、重复激活、跨线程和错误归属有确定结果。
- 缓存及请求去重沿用 asset_manager；不引入隐式阻塞或第二份资产记录。
- 编译全部目标，运行定向 CPU 测试；本任务不运行大场景、GPU 或远端 CI。

## 非目标

协程框架、流送、GPU 上传完成等待、事务回滚、跨 World 迁移和版本发布。
