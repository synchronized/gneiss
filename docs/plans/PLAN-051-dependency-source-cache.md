<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 0.51：第三方源码获取与共享缓存

## 状态与优先级

2026-10-20，用户确认优先实施。CAORS 材质装配暂缓，已有成果保留。
目标是不同 preset 复用固定版本源码，编译产物保持隔离；不在本任务升级依赖版本或改变链接方式。
CAORS 改共享库单独验证，不与源码迁移混为一次改动。

## 已确认策略

- 保留 EnTT、yyjson、libspng、miniz 四个 submodule。
- 九项下载型依赖迁往仓库级 vendor/cache；该目录不提交 Git，可由宿主覆盖根路径。
- Granit 保留 AUTO/PACKAGE/FETCH；所有依赖保留显式本地源码覆盖，联合开发不修改缓存源码。
- 共享归档及源码；不共享 FETCHCONTENT_BASE_DIR、CMakeCache、对象、生成头或库文件。
- 锁定版本及 SHA256；缓存键包含内容摘要，不能仅凭库名或可变分支命中。
- 下载、解压和发布持有每个缓存项的进程锁；先校验临时归档，解压完整后才发布。
- 默认允许联网；离线模式只接受完整源码缓存或已有校验通过的归档，否则明确失败。
- 源码缓存约定只读。命中时验证完成标记及入口文件，不声称每次重新扫描所有源码。
- 来源缓存、构建链接方式、安装部署是三个独立维度；普通公共 ABI 不因缓存迁移而改变。

## 目录

```text
vendor/cache/
  archives/<name>-<sha256>.tar.gz
  sources/<name>/<sha256>/
  locks/
  staging/                     未完成下载或解压，不作为有效源码
build/<preset>/_deps/          每个配置自己的构建目录
```

配置名：GNEISS_DEPENDENCY_SOURCE_CACHE_DIR；离线开关：GNEISS_DEPENDENCY_OFFLINE。
归档方式迁移需确认子模块及生成源文件完整，不能将 Git 仓库归档等同递归 checkout。
本地 FETCHCONTENT_SOURCE_DIR_<NAME> 优先于下载；宿主自行负责该可变目录内容。

## 依赖范围与顺序

| 阶段 | 依赖 | 工作 |
| --- | --- | --- |
| 1 | libuv | 已有发行归档及摘要，先验证基础设施与一个真实依赖 |
| 2 | ImGui、ImGuizmo、MikkTSpace、bc7enc_rdo、Tracy | 将固定提交转换为已验证归档，保持目标与链接模式 |
| 3 | fastgltf | 同时检查 simdjson 等传递获取行为和源码目录写入 |
| 4 | Granit、CAORS | 保留 package/源码覆盖，核对递归子模块与安装；统一 Granit 缓存根目录 |

每阶段完成后检查源码不写入配置相关产物，两个独立构建目录共享源码但不共享 binary dir。
不自动删除旧 build/_deps 或用户缓存；迁移不依赖删除用户构建目录。

## 验收

- 本地归档测试：首次填充、重复使用、不同摘要隔离、损坏摘要拒绝、离线命中与缺失失败。
- 两个 configure 并发填充同一项，只发布一个完整目录；失败残留不当作成功。
- 同一依赖跨 Debug/Release 或独立构建目录共享源码，各自生成构建产物。
- 显式源码覆盖不访问网络；下载失败错误含依赖与缓存位置。
- libuv 及受影响的输入/VFS/Application 测试；后续各依赖迁移再跑相应构建。
- 不以本机缓存命中代替干净缓存验证，不把下载加速描述为运行时性能提升。

## 实施记录

已实现共享归档缓存基础设施及 libuv 首批接入；其余阶段尚未迁移。
本地归档脚本已验证首次填充、复用、离线缺失/归档解压、损坏摘要拒绝和双进程并发。
基础设施失败时保留 staging 诊断材料，不执行递归删除。

首批本地验收：主 Windows Clang Debug 配置与 Engine/相关目标重建通过；VFS 桥、真实
Application、材质文档、缓存脚本共 4 项 CTest 通过。另验证同名不同摘要保留独立源码目录。
两个独立 Debug/Release 小型 consumer 在离线模式下解析到相同 libuv 源码和不同 binary dir；
显式源码覆盖成功且没有创建指定缓存目录。这两项是 configure 验证，非完整 Release 构建。
未跑完整安装矩阵、Linux 或其他八项下载依赖迁移。第三方 libuv 构建仍有上游警告，未关闭警告。
