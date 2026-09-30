<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-287：VFS 读取来源基础

## 交付范围

2026-09-30 完成内部 VFS 原生读取来源，不改变公开 C/C++ SDK 或纹理磁盘格式。
设计依据为 [ADR-052](../decisions/ADR-052-texture-residency-and-budgets.md) 的读取来源/容器分层。

- `file_system::open_read` 默认返回 UNSUPPORTED，不通过整文件读取模拟区间能力。
- `virtual_file_system::open_read` 沿用 URI 校验与最长挂载点选择，失败清空来源输出。
- `native_file_system` 打开原生文件并从打开对象取得长度；来源以独立所有权持有流。
- `read_source::read_at` 读取调用者指定的范围和缓冲，检查越界及整数范围；空读取可位于 EOF，
  短读返回 IO。同来源并发读取串行保护游标，VFS 和后端销毁不影响已打开来源。
- 来源不保证内容不可变，不能代替摘要、版本身份或最终一致性校验。

## 验证

Windows Clang Debug 构建启用警告作为错误。初轮配置缺少 Threads 导入目标，随后补齐；
独立测试编译缺少库实现导出定义，已按现有测试约定修正，未关闭任何警告。

```powershell
cmake --build --preset windows-clang-debug --target gneiss_read_source_test gneiss_resource_service_test gneiss_source_revision_test
ctest --test-dir build/windows-clang-debug -R 'gneiss\.(read_source|resource_service|source-revision)$' --output-on-failure
```

最终相关回归 3/3 通过。新测试覆盖未初始化、缺失文件、空文件、非法 URI、挂载优先级、
旧后端不支持、失败清空、旧整文件接口、VFS 销毁后的生命周期、越界和最大偏移、
并发读取、外部截断的短读及错误后再次读取。

## 未完成范围

截至首轮读取来源交付，版本追踪包装、设备变体选择及加载服务尚未接入新入口。
现有 Runtime 仍走整文件纹理读取，不能宣称内存峰值已经降低。
本轮仅本地 Windows 验证，未运行 Linux、发布矩阵或 GPU 场景验收。

## 纹理容器读取器（2026-09-30）

新增 `texture_container`，拥有读取来源，打开时仅读取 64 字节封装头、受预算限制的 Manifest
及最多 15 字节填充；后续 `read_payload` 使用 Payload 相对偏移读入调用者缓冲，不分配负载。
Manifest 的纹理语义仍由渲染适配层验证，此处只验证封装布局，不解释设备格式或选择 Mip。

头部校验抽取为 `decode_texture_binary_header`，旧整文件解码共用它，保持格式版本不变。
校验覆盖来源长度、保留字段、偏移及尺寸溢出；填充由两条读取路径分别检查。
错误消息改为在捕获异常的函数内部复制，避免分配错误消息时异常逃离 noexcept 边界。

计数来源测试使用 1 MiB Payload、3 字节 Manifest：打开累计读取 80 字节，随后请求 4 字节
负载，累计变为 84 字节。Manifest 预算不足时只读取头部，失败清空容器并销毁来源。
覆盖每阶段 IO 失败、负载错误后重试、空/越界读取、非法版本和布局、整数极值、截断、
非零填充、无填充布局及析构释放。旧整文件格式测试继续通过。

Windows Clang Debug 引擎与测试构建通过；相关 CTest 5/5 通过（resource_service、read_source、
texture_binary、texture_container、source-revision）。本轮没有接入版本一致性检查或实际 GPU 加载，
因此不能作为场景加载内存下降的证据。

## 分块内容校验（2026-09-30）

版本追踪 VFS 现在提供 `open_read`：先以固定 64 KiB 缓冲计算完整来源 SHA-256 并登记长度与摘要，
再交出来源。最终 `verify` 对支持区间读取的后端重新打开路径，分块校验长度与摘要，块间检查取消。
只有后端明确返回 UNSUPPORTED 时才兼容原有整文件校验，IO 或其他错误不会通过降级读取掩盖。

这条最终校验路径已由现有场景加载使用；纹理准备尚未切换到容器读取器。分块哈希限制的是临时缓冲，
仍需读取完整文件，不代表减少完整性校验的 I/O。来源也不提供原子内容快照，外部原地修改与 ABA
变化的限制仍与原有版本校验一致，不宣称可以抵抗任意并发修改。

新增增量 SHA-256 与单次摘要共用实现，digest 可重复计算及继续追加。测试覆盖已知向量、百万字节
向量、各分割位置、55/56/63/64 等填充边界、空更新和中间摘要。版本测试使用 200,000 字节来源，
确认最大读取 65,536 字节、4 次读取完成建账，随后检测等长变化、长度变化、IO 失败及块间取消。

Windows Clang Debug 相关回归 6/6 通过，覆盖 SHA-256、资产构建、场景加载、读取来源、容器与版本账本；
修改文件的 clang-tidy 无告警。

包路径审计确认：当前发布包为目录，ZIP 仅用于分发，见[发布包格式](../reference/package-format.md)。
本版使用原生 VFS 覆盖目录包，不创建额外 ZIP 后端；前期“包后端待接入”的表述已在计划中修正。

日常 PNG Sponza 的 Windows Debug 首载单样本成功，报告及二进制哈希保存在
[运行报告](artifacts/0.44-streamed-verify-daily.json)：耗时 96.266 s、事件 P95 16.395 ms、最大
102.756 ms、激活 0.922 ms、候选 464,453,832 B、505 RID。包含回读后的进程峰值为
1,447,993,344 B；日志在较早时间输出的峰值不与最终值混用。回读 SHA-256 与 0.43 的
`e994ae7d16438a84388dd9720f7f96a84b0a247df9838a9e469d00e917aaf401` 一致。
这是兼容回归样本，耗时未显示提升，不作统计性能结论，也不代替完整 4K Cook 验收。
