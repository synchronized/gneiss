<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-329：资产身份、版本与租约审计

2026-10-15，基于 `1571549` 的静态源码审计；本轮不修改加载行为或迁移缓存。
目标见 [共享资产实施设计](../plans/asset-scene-ownership.md)。以下不是完整架构审计验收。

## 当前证据

源码路径均相对仓库根：

| 源码与入口 | 已确认行为 | 对共享资产的影响 |
| --- | --- | --- |
| `src/engine/asset/resource_cache.cpp`：acquire/observe | 以 URI 原字符串查 map；type 是命中后的校验；loading 命中返回 INVALID_STATE | 尚无来源域、多版本键或共享任务订阅，不能直接作为并发请求合并机制 |
| 同文件：reload/reload_transaction | 创建新条目，成功后替换映射；事务使用暂存 map；旧引用可继续持有旧条目 | 已有候选发布机制可复用，但不等于所有发布路径都保持旧资产版本 |
| 同文件：revision/release_unused | revision 是缓存变更序号；无外部引用的条目在显式清理时移除，并重复扫描依赖链 | revision 不是文件内容版本；强依赖环不受这种扫描自动解决 |
| `src/engine/asset/asset_uri.cpp`：validate_uri | 校验 UTF-8 与 asset:// 路径，拒绝空段、点段、反斜线等；不做大小写/文件别名统一 | 现有规则不能描述为完整文件身份规范化，不能按 Windows 规则全部转小写 |
| `src/engine/asset/virtual_file_system.cpp`：open_impl/mount | 最长挂载前缀选择后端，交给后端解析相对路径 | 相同 URI 在不同 VFS/来源域可能是不同内容；缓存键未表达该区别 |
| `src/engine/asset/source_revision_file_system.hpp` | 私有账本按路径记录字节数与 SHA-256；最终复读检测变化 | 不是共享资产版本接口，也不提供跨进程文件原子快照 |
| `src/engine/asset/texture_preparation_data.hpp` | profile 包含不透明 generation 和格式能力数组；零值保留兼容路径 | 不是内容版本或稳定磁盘身份；准备产物兼容性需要单独表达 |
| `src/engine/function/render/render_asset_loader.cpp`：acquire_cached | 对 texture 及 material 的纹理依赖校验 profile，存在零代次/无 manifest 的兼容分支 | 不能据此宣称所有命中都具有严格设备版本键 |
| 同文件：mesh_asset/material_asset/texture_asset | 租约持有缓存条目；资产析构调用借用的资源服务销毁 RID；material 持有纹理条目引用 | 依赖寿命已有基础，资源服务必须晚于全部相关租约销毁 |

## 发布路径存在两种语义

`render_asset_loader::reload_assets` 按纹理、材质、Mesh 顺序在暂存缓存加载新资产，成功后交换
缓存映射。`tests/render/render_asset_loader_test.cpp` 检查重新获取的三个资产 RID 与旧 RID 不同。

`publish_assets` 对已有条目校验 expected 条目与 previous 数据身份，随后调用
`replace_texture/replace_mesh/replace_material`，保持 RID 并替换其数据；材质依赖引用也会更新。
`publish_textures` 同样存在保留已有 RID 的更新路径。帧快照可以持有旧数据，但旧场景下一帧
通过相同 RID 查询时可能看到更新。这与“场景使用引用固定资产版本”不是同一契约。

所以共享缓存迁移不仅是改变对象拥有者，还涉及发布行为。当前场景域隔离仍需保留，直至共享
路径具备独立候选版本和明确的使用者切换机制。此结论不是断言当前隔离实现已经污染旧场景。

## 待讨论方案

1. 身份：建议首批使用运行期来源域身份、合法 URI 和资产类型；重挂载需更新来源身份。
   同 URI 不跨来源域合并，暂不引入持久 GUID/侧车文件及改名追踪。另一方案是直接引入持久
   AssetId，改名稳定性更好，但牵涉导入产物、项目格式和迁移，超出最小共享闭环。
2. 版本发布：建议新场景准备使用固定版本引用，候选变更分配独立资源，成功激活后切换使用引用。
   显式热重载作为独立事务，明确影响哪些使用者；本轮不静默改变其已有契约。另一方案是保持
   一个共享 RID 并更新其数据，消费者更新简单，但无法同时让旧场景保持旧版本，仍需额外隔离。

建议依赖版本也参与派生资产版本判定：材质文件未变但纹理变更时，不能只按材质文件摘要认定
全部产物相同。具体版本键、源摘要复用和热重载使用者切换协议仍需设计，不能将建议当作实现。

## 验证范围

已核对缓存、VFS、来源账本、profile、租约析构、两种发布路径及现有测试断言。
未重新执行测试或性能采集，未修改源码；跨场景共享、并发去重与版本隔离尚未实现。
发布方案确认前不迁移共享缓存，也不改变公开 RID 或热重载语义。
