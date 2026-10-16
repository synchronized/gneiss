<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：场景激活时提交版本选择

日期：2026-10-16。共享资产所有权迁移的前置步骤，缓存仍由场景域拥有。

## 变更

`resource_cache::prepare_adoption` 先校验版本、准备选择映射并保留候选租约，不修改默认选择。
返回的 RAII 事务只在所属线程使用且必须早于缓存销毁；销毁即取消，`commit` 无分配交换映射。
缓存不可复制或移动，避免在途事务借用失效。

事务存续期间独占采用权，其他采用返回 `NOT_READY`，普通查询和固定版本发布不受影响。
这不是线程锁，也不是后台任务等待；宿主仍通过已有加载忙状态限制交互请求。
原 `adopt_versions` 的回调接口复用同一事务，回调失败或异常释放采用权。

场景加载到 `ready` 时只准备事务，`take_candidate` 在激活安全点提交选择。
取消、失败、加载服务析构先销毁事务，再销毁候选域；候选版本不会提前成为默认选择。

## 验证

Windows Clang Debug：`gneiss.resource_service`、`gneiss.scene-load-application`、
`gneiss.editor.prefab-edit` 三项通过。覆盖准备不改变选择、取消释放采用权、竞争请求拒绝、
候选租约保活、重复提交、递归采用拒绝、ready 取消、错误请求 ID、场景激活及重试。
`resource_cache.cpp` 的 clang-tidy 无告警，`git diff --check` 通过。

未运行完整平台和发布矩阵，本轮不改变公共 ABI。

## 后续迁移

缓存与类型加载器尚未迁到 Application。Prefab 的 `install_prepared` 仍使用旧 URI 槽位，
同 URI 已存在时拒绝安装；必须先改为具有来源身份的不可变版本，避免共享后再次打开场景失败。
旧同步重载的可变 RID 路径也不能不加区分地接入共享版本缓存。

关联：[共享资产方案](../plans/asset-scene-ownership.md)、
[ADR-064](../decisions/ADR-064-shared-assets-and-scene-instances.md)。
