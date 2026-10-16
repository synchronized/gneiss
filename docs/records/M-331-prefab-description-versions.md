<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-331：准备好的 Prefab 描述使用不可变版本

日期：2026-10-16。共享资产迁移的前置步骤，不代表 Application 共享所有权已完成。

## 实现

`prefab_asset_loader::install_prepared` 不再占用旧 URI 单槽位。它仅解析 VFS 挂载身份，不读取文件，
按来源身份、URI、Prefab 类型及描述摘要获取不可变条目。同版本复用，不同版本并存，旧租约保持有效。
未挂载的 URI 返回 `NOT_FOUND`，非法 URI 或空对象列表返回 `INVALID_ARGUMENT`，失败清空输出。

版本摘要使用 SHA-256，逐字段编码描述 Schema、UUID、作者 JSON、依赖 URI、对象层级、名称、
变换、相机和网格渲染描述。字符串使用长度前缀，整数使用固定宽度小端编码，浮点使用位模式，
不读取 C++ 对象填充字节。新增描述字段时必须同时更新版本编码。

不能只摘要 `author_json`：准备之后修改相机或变换也必须产生新版本。作者文件序列化器也不能作为
通用版本编码，因为其部分修改要求原作者文档已含对应组件。本轮未改变作者保存规则。

描述版本只包含依赖 URI；网格、材质与纹理版本仍由实例的独立资产租约固定，不把后端资源下沉到描述。
发布描述版本不改变默认采用选择。旧同步 `acquire/reload` 暂时保留旧 URI 路径，与候选版本分别寻址；
这只是迁移中状态，不能据此宣称同步入口或共享资产体系已收敛。

Prefab 历史快照冻结时通过有效 Application 取得真实 VFS 来源身份，不再使用未挂载的临时 VFS。
临时缓存释放后，快照中的租约仍独立持有不可变描述。

## 验证

Windows Clang Debug：8 项测试通过，包括 Prefab 描述、运行时实例、场景构建、异步场景加载、
Editor Prefab 编辑、作者操作、C++ 刷新及 Editor smoke。

新增断言验证同来源 VFS 副本复用、不同挂载来源隔离、同 URI 新旧版本共存、
准备后修改变换产生新版本、不触发文件读取、不覆盖旧同步缓存、错误时清空输出和未使用版本回收。
`prefab_asset_loader.cpp`、`prefab_placement.cpp` 的 clang-tidy 无告警；`git diff --check` 通过。

未执行 Linux、完整消费者矩阵和大 Prefab 延迟测量。摘要仍在所属线程安装阶段计算，耗时随描述规模增长。

## 剩余工作

迁移旧同步消费者及默认选择、将缓存和加载器所有权移到 Application，
验证跨场景实际资源复用、取消隔离与退出顺序。

关联：[共享资产方案](../plans/asset-scene-ownership.md)、
[延迟采用事务](M-331-deferred-version-adoption.md)。
