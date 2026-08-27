<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 2026-08-27 静态几何 Arena 验收

## 结论

Granit 渲染服务已将逐帧 Vertex/Index Buffer 构建改为持久静态几何 Arena。Mesh 首次出现在
Render Snapshot 时写入 Arena；多个对象实例复用同一份几何数据，仅通过动态 Uniform Offset
区分 Transform 与材质颜色。Mesh RID 失效或 generation 更新后，旧镜像会被清理并重新打包。

## 性能数据

在同一台 Windows 开发机、Clang Debug 和相同 Granit 提交上，分别对改动前提交 `16e3247` 与
当前实现执行 1 次预热和 10 次 Lantern Gallery 三帧 Smoke：

| 实现 | 运行阶段中位数 | 运行阶段平均值 |
| --- | ---: | ---: |
| 每帧重建几何 Buffer | 44.45 ms | 50.50 ms |
| 持久静态几何 Arena | 26.43 ms | 29.76 ms |
| 变化 | -40.5% | -41.1% |

数据只用于确认当前改动的量级，不作为跨平台性能承诺。Debug、驱动缓存、窗口系统和系统负载都会
影响结果；正式基准仍需独立的稳定帧采样与统计设施。

## 验证范围

- 同一 Mesh 的两个对象使用同一几何 Arena 和不同动态 Uniform Offset。
- 已索引与无索引 Mesh 均转换为持久 UInt32 Index 数据。
- Mesh 销毁并复用 RID 槽位后，generation 变化会触发旧镜像清理和新镜像上传。
- Temple、Lantern Gallery 与 Granit 平台 Smoke 均通过。
- Windows Clang Debug 完整测试通过。
