<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 渲染资源模型入口审计与统一运行验收

日期：2026-10-26。对应 [PLAN-054](../plans/PLAN-054-render-resource-model.md) 阶段一、二。
本次按用户后续授权执行入口审计、重复状态审计和统一运行验收；不启动流送阶段。

## 入口与所有权审计

- `render_service::implementation` 唯一拥有 CPU 资源表；Application 的 `resources()` 为内部借用。
  保留 Loader 采用数据和程序化资源创建所需的底层入口，不增加一层同义转发接口。
- 规范化 `reference_mesh/texture/material` 复用同一控制块；将 `make_runtime_resource` 收为私有，
  防止调用者绕过规范入口为同一 RID 建立第二个所有者。
- 增加程序化纹理的强引用创建重载，创建失败清空输出并清理临时槽位。编辑器字体改走该入口。
  ImGui 整数 Texture ID 只在适配边界解析，进入 UI 绘制命令后由 texture 强引用保活。
- 统一缓存是来源域、URI、类型到资源的索引；Render 表是 RID 到当前 CPU 内容及弱身份的映射。
  两者键和职责不同，未合并成另一套缓存，也未新增所有权表。
- 运行材质持有纹理依赖；不可变 material_data 和帧快照固定提交时内容。后端镜像保存 GPU 状态。
  这些是不同生命周期的数据，不是需要删除的重复资源所有者。
- Scene/Prefab 作者绑定、暂存和撤销引用保留其编辑语义；没有重新加入全场景保活集合。

## 发现并修复的问题

1. 同步材质重载误将依赖优先批次的第一项当作目标材质，且同步加载逆序采用。
   改为按准备结果的依赖顺序采用，移除重复的纹理依赖队列构造。
2. Application 更新回调失败未投递诊断和日志；现在报告一次 application 类别错误并返回原结果。
3. 编辑器改名在外线程把线程错误误判为参数错误；现在先返回 invalid_state。
4. 旧测试保留已销毁 World 借用指针、在最后强引用释放后继续使用裸 RID，或要求 CPU 加载完成后
   第一帧必然已有新纹理。改为遵循对象生命周期，并在有限超时内验证真实像素变化。
5. 校正迁移后过时的反射 schema、VFS 挂载时机、受限读取 mock、同步实例化取消阶段、
   输入输出清空约定、IPC 阶段数量和包校验夹具；Prefab 测试接入 Application 异步执行器。
   边界检查先清空生成夹具内残留的命名空间反例，保证重复运行不污染合法基线。

未禁用测试目标或放宽像素正确性断言；纹理占位、更新后颜色、失败保旧和无效 RID 检查仍保留。

## 验证结果

环境：Windows、Clang、Debug、Granit 图形后端；警告视为错误。

- 全量构建通过；无 Granit 平台的 Engine 构建通过。
- 完整 CTest **190/190 通过**，37.27 秒。首轮为 172/190，通过定位修复后完整复跑。
- 覆盖资源引用、ECS/帧保活、工作线程最后释放与关闭竞争、异步加载、场景切换与取消、
  像素、编辑器和工具回归；像素用例涵盖线程池及协作执行。
- 修改文件 clang-format、Core/Public 边界检查及 git diff --check 通过。
- 本次未运行远端 CI，未修改 Granit，未执行其他平台、Release 性能或硬件设备丢失注入。

本地日志位于 `build/asset-migration/`：`review-build7.log`、`review-headless.log`、
`review-full-final.log`；原始日志和图像是构建产物，不纳入版本控制。

## 真实场景

使用外部缓存 `D:/sunday/workspace/asset-cache/intel-sponza/0.43-daily-assets-2`，执行
`gneiss_scene_load_baseline <资产目录> <输出前缀> <thread|cooperative> initial`。

| 指标 | 线程池 | 协作执行 |
| --- | --- | --- |
| 退出结果 | 成功 | 成功 |
| 场景结构加载时间 | 331.19 ms | 279.07 ms |
| 加载并排空时间 | 123.45 s | 117.27 s |
| 完成截图校验时间 | 125.40 s | 119.17 s |
| 节点 / 活动资源 / 旧资源 | 553 / 505 / 0 | 553 / 505 / 0 |
| 峰值进程驻留内存 | 1,454,153,728 B | 1,452,896,256 B |

数据文件前缀：`review-sponza-thread`、`review-sponza-cooperative`（JSON、CSV、PPM、日志）。
结构 ready 与全部资源可见不是同一个时间点；不能把 279–331 ms 宣称为整个场景加载耗时。
这是功能回归，未满足或评估正式性能目标。initial 不覆盖真实场景切换、设备丢失、最小化恢复矩阵；
小场景切换和取消由测试集覆盖。低内存最后释放仅审计了无分配入队实现，未做分配器故障注入。

## 后续边界

阶段一、二的本地功能回归完成。CPU 数据释放/恢复策略、Mip/LOD 流送和性能扩展仍属后续阶段。
真实设备丢失、低内存故障注入、跨平台和 Release 性能矩阵不由本记录宣称通过。
