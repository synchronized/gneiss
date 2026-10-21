<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# CAORS 同步/异步重载发布顺序需求

日期：2026-10-22。状态：已在 Gneiss 锁定依赖复现，方案待确认；未修改或向上游提交。

## 背景与范围

Gneiss 正准备将同步输入加载与异步加载收敛到同一个 CAORS cache/GUID。
当前同步入口只返回准备结果，尚未发布到该缓存，因此下面的问题是接入障碍，
不是已经证明 Gneiss 当前输入会发生版本回退。

验证版本：CAORS 0.7.0，提交 `3b543eb1549156a21e432af91da3abd98727fff8`。
源码由 Gneiss vendor/cache 固定，不代表已验证上游之后的提交。

## 确定性复现

源码：[sync_publication_probe.cpp](../../tests/integration/caors/sync_publication_probe.cpp)。
无文件、窗口、计时等待或后台线程；执行队列与完成凭证手动推进。

```powershell
cmake --build build/windows-clang-debug --target gneiss_caors_sync_publication_probe
./build/windows-clang-debug/bin/gneiss_caors_sync_publication_probe.exe
```

步骤：注册同一 cache 和可控 Loader；旧异步请求启动但保留完成凭证；同步加载版本 2；
再交付旧异步版本 1，排空完成队列后查询缓存。重载用例预先建立版本 0。
第三个用例再添加独立订阅，并在 worker 启动前取消其中一个订阅。

Windows Clang Debug 实际输出：

```text
first-load: sync=2, after_old_async=2, sync_handle=2, stale_overwrite=no
reload: sync=2, after_old_async=1, sync_handle=2, stale_overwrite=yes
reload-cancel-one: sync=2, after_old_async=1, sync_handle=2, stale_overwrite=yes
```

已有缓存的同步重载会被旧异步重载覆盖；取消单个订阅不影响独立订阅，因此无法普遍解决。
同步得到的强引用仍有效，发生回退的是 cache 当前绑定，而非已持有对象被修改。
首次加载行为不同，不能只验证首次加载就认定同步/异步已安全统一。

该工具是能力诊断，不加入 CTest，不将缺陷行为写成必须保持的产品断言。
退出成功仅表示复现流程正常完成，是否存在回退看 stale_overwrite 输出。

## 原因与建议的最小上游改动

resources/database.h 的 reload_handle 转交 resources/io.h 的 reload_handle_into，
后者调用 Loader 并直接 cache.replace；异步刷新另有发布资格选择。
建议让同一 cache/GUID 的同步与异步发布遵守一致的过期判定。

建议 PR 目标：统一同步加载/重载与异步刷新发布资格，而非增加 Gneiss 专用 API。
优先复用 CAORS 内部现有 publication 机制，不要求暴露其 detail 类型。
需明确 load_handle_into、reload_handle_into、database/asset_manager 入口是否一致覆盖；
低级 cache.replace/erase 的行为也需文档说明，避免上层绕过版本规则。

建议行为：成功同步发布后，之前启动的异步任务不得回写 cache 当前绑定；
旧订阅可以依既有协议取得其旧版本或被标为过期，但不能让缓存回退。
同步失败保留当前缓存；是否撤销旧异步发布资格需要上游明确，不能由 Gneiss 猜测。
后启动的新异步请求仍可正常更新；不同 GUID 互不影响。

验收至少覆盖：首次加载、已有缓存重载、同步失败、单订阅取消/独立订阅保留、
同步后新异步、旧强引用存活、完成队列延迟与直接资源函数入口。
同步实现不等待外部执行器、不排空任意宿主队列，也不使用全局取消。

## Gneiss 接入步骤与方案选择

推荐 A：先由 CAORS 修复/明确上述发布契约，再接入同类型同步 Loader，共享 GUID 和缓存，
保留来源复验、失败不改输入、非阻塞异步采用与无线程模式验收。
上游修复后重跑诊断，再增加期望“不回退”的正式回归测试。

备选 B：Gneiss 在有在途资源工作时拒绝同步加载，要求调用方完成异步流程后重试。
实现较快，但改变同步接口成功条件，影响调用方，且可能把其他资源工作纳入互斥。
不建议通过阻塞等待、重建 manager 或加入第二套发布代次来绕过问题。

当前不实施 A/B 中改变运行时语义的部分，等待用户确认；其他互不依赖的布局迁移不受影响。
