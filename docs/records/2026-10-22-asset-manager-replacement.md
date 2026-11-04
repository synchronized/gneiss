<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# CAORS 替换验收记录

日期：2026-10-22。范围：[迁移计划](../plans/PLAN-051-asset-manager-replacement.md) R0～R3；
决策：[ADR-071](../decisions/ADR-071-example-asset-manager.md)。本轮不发布版本、不修改 Granit。

## 实际替换

依据 Granit 锁定提交 `c9803195cc81d2d1cef55e47f5bcff1c42884ccd` 的
`examples/common/assets` 最小模型移植，未直接链接示例 target。
复用类型/URI 请求表、显式 Loader、共享请求观察和只读结果模式；
保留原 MIT 版权，新增随安装分发的 `3rd/granit-example-assets-LICENSE`。

| 文件或目录 | 处理结果 |
| --- | --- |
| `src/engine/resource/asset_manager.*` | 扩展既有管理器，显式注册 Loader，按类型/URI 合并请求与缓存 |
| `src/engine/resource/asset_request.hpp` | 独立观察者、取消与一次性领取 `shared_ptr<const T>` |
| `src/engine/resource/loaders/asset_loader.hpp` | 显式类型、字节上限与 CPU 解码接口，不要求反射或资源基类 |
| `src/engine/resource/vfs/asset_source.*` | 接入既有 VFS，分步异步打开/读取/复验/关闭；读取块最多 64 KiB |
| `src/engine/resource/cache`、`types` | 材质改普通共享只读数据，Mesh 删除资源包装，保留确切版本与租约语义 |
| `src/engine/platform/caors`、`src/engine/resource/backend/caors` | 全部删除 |
| `tests/integration/assets` | 常规构建接入 application、manager、source、material 四项行为测试 |
| `tests/integration/caors` | 删除依赖专属探针；仍需要的行为迁入上述测试 |
| CMake、安装配置与第三方声明 | 删除 CAORS 获取、链接、包查找、探针选项与 DLL 分发 |

输入继续通过 `load_input` / `request_input` 接入；材质和 Mesh 的 CPU 缓存改用普通数据持有。
Prefab、纹理和固定版本加载没有在本轮强行改成泛型 URI 请求；所有这些路径已不依赖 CAORS。

与示例的主要差异：使用 Gneiss VFS、错误类型和任务执行器；无线程时协作推进，
不引入示例线程池；补充同步加载、来源复验、同步/异步发布顺序、关闭和任务拒绝语义。
没有移植无当前消费者的分组进度等设施，也没有新增递归依赖框架。

同步成功会使同键更早的异步工作失去当前缓存写入权，但旧观察者仍可领取自己的结果；
同步失败不撤销原异步工作。失败不覆盖已成功缓存。单个观察者取消不影响其他观察者；
全部观察者离开后不继续分步工作或发布缓存，不承诺立即中断已提交的 I/O。
关闭只关闭管理器任务作用域；已领取的只读结果可独立存活。

## 验证结果

环境：Windows、Clang Debug，开发配置警告视为错误；Web 使用 Emscripten 与 Node。
本地原始日志位于忽略目录 `build/asset-migration/`，不作为安装内容或提交产物。

| 验证 | 结果 |
| --- | --- |
| Windows 完整 Editor / Runtime / Granit 构建 | 通过 |
| 最终完整测试 `ctest --test-dir build/windows-clang-debug --output-on-failure -j 4` | 201/201 通过，57.87 秒 |
| 干净静态核心、关闭图形平台与线程的安装消费 | C/C++ 等 4/4 通过；增加 manager/application 的协作消费 6/6 通过 |
| 干净共享核心、关闭图形平台与线程、离线配置的安装消费 | 4/4 通过 |
| Web 核心构建、独立安装消费及 MEMFS 输入/manager | Node 下 6/6 通过 |
| 新资源测试 | 合并、取消、乱序、失败、关闭、源变化、超限、短读、异步读/关、版本保活与批量失败通过 |
| clang-tidy | manager、asset_source、cpu_document_cache 三个实现单元检查通过 |
| 格式、`git diff --check`、核心边界检查 | 通过 |
| 生产源码、生成安装 CMake 与安装文件名扫描 | 未发现 CAORS 依赖；共享核心导入表无 CAORS DLL |

第一次最终全量验证与其他配置编译同时执行，两个编辑器流程在场景加载等待时限处失败；
未修改测试时限，单独复测 2/2 通过，随后停止竞争构建负载、同样以 `-j 4` 完整复测 201/201 通过。
据此判断本次失败与负载相关；该记录保留失败事实，不宣称证明所有时序问题都不存在。

扫描有意保留历史文档和阻止 CAORS 再引入的负向边界测试。
旧构建缓存不作为分发证据；静态、共享、Web 安装检查均使用本轮隔离前缀。

## 剩余问题与验证边界

请求缓存目前强持有成功结果至管理器关闭，尚无有界淘汰策略。
来源复验、版本索引、批量采用、CPU/GPU 状态和历史保活继续保留；
这些语义简化、对象命名与反射需求审查见计划 R4，未在本轮宣称解决。

没有迁移前后同条件性能/内存基线，不能据代码删除量宣称更快或更省内存。
尚未验证 Linux/macOS、真实浏览器 fetch、Web Editor 或完整远端平台矩阵；
Node/MEMFS 通过不能替代这些验证。本轮未推送、未创建 PR、未发布。
