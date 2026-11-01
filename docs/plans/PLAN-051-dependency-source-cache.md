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
| 4 | Granit、CAORS | 保留 package/源码覆盖，核对递归子模块与安装；隔离 Granit 附属缓存 |

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

四批默认源码接入已完成（九项直接依赖及 simdjson 传递依赖）；以下按阶段记录验收。
首批实现共享归档缓存基础设施及 libuv 接入。
本地归档脚本已验证首次填充、复用、离线缺失/归档解压、损坏摘要拒绝和双进程并发。
基础设施失败时保留 staging 诊断材料，不执行递归删除。

首批本地验收：主 Windows Clang Debug 配置与 Engine/相关目标重建通过；VFS 桥、真实
Application、材质文档、缓存脚本共 4 项 CTest 通过。另验证同名不同摘要保留独立源码目录。
两个独立 Debug/Release 小型 consumer 在离线模式下解析到相同 libuv 源码和不同 binary dir；
显式源码覆盖成功且没有创建指定缓存目录。这两项是 configure 验证，非完整 Release 构建。
未跑完整安装矩阵、Linux 或其他八项下载依赖迁移。第三方 libuv 构建仍有上游警告，未关闭警告。

### 第二批：工具与编辑器依赖

ImGui、ImGuizmo、MikkTSpace、bc7enc_rdo 默认版本及 Tracy 已接入共享归档缓存。
保持原提交，归档从对应提交的 codeload URL 获取并固定 SHA256；本地源码覆盖优先。
这些接入使用项目自身定义的编译目标，不调用归档内上游 CMake，避免引入额外构建行为。
bc7enc 的自定义仓库/提交继续走原 Git 获取，离线必须显式提供本地源码，不静默回到默认版本。
四项原静态目标和 Tracy 共享目标的链接类型不变。Granit、CAORS、fastgltf 尚未迁移。

第二批本地验收：Editor、assetc 重建成功；assetc inspect/cook、隐藏 Editor 冒烟、缓存脚本
共 4 项 CTest 通过。另建立启用 Tracy 的独立 Debug consumer，五项依赖均构建通过，
Tracy 保持共享库。Debug/Release 两次离线 configure 使用相同源码、不同 binary dir；
五项显式源码覆盖均成功，未创建指定的备用缓存目录。Release 仅配置，未完成构建。
自定义 bc7enc 离线且未提供本地源码时明确拒绝。未验证 Linux、完整安装与运行时性能。

验证时过长的临时构建目录触发 Windows 260 字符限制（FetchContent 子构建），换用较短
`build/sc-custom` 后完成自定义仓库拒绝测试；本轮不宣称解决所有 Windows 长路径限制。

### 第三批：fastgltf 与 simdjson

fastgltf 默认固定提交已接入归档缓存，自定义仓库保留 Git 路径，离线需显式源码覆盖。
发现上游原配置会下载 simdjson 到源码树，故同步将其原有 3.12.3 版本纳入独立归档缓存，
从 singleheader 源构建私有静态 target，提前提供 simdjson::simdjson 以跳过上游下载。
保留宿主已提供 target 的行为，不修改上游源码。上游仍生成安装导出，因此私有 simdjson
链接限定构建树；Gneiss 不安装 fastgltf SDK，不能将此配置作为独立 fastgltf SDK 发布。

本地 Windows Clang Debug：assetc 与 glTF 导入测试重建成功；glTF importer、assetc
inspect/cook、缓存脚本共 4 项测试通过。独立 Debug/Release 离线 configure 共用源码、
隔离构建目录，本地源码覆盖不触碰缓存。逐文件比较两个源码缓存与下载归档，除缓存完成
标记外没有新增或修改文件，未出现上游源码下载写入。Release 未构建，Linux、完整安装未验收。
目前原九项直接下载依赖中已完成七项；额外处理一项传递依赖 simdjson。Granit 与 CAORS 尚未迁移。

### 第四批：Granit 与 CAORS

默认固定版本的 Granit 与 CAORS 已迁移到校验归档，版本和静态/共享链接模式均未变化。
保留 Granit PACKAGE/AUTO 路径、自定义 Git 仓库路径和两个依赖的显式本地源码覆盖。
当前归档不依赖递归子模块；CAORS 已启用功能没有额外下载。
Granit 的 SDL3/ImGui 获取保持关闭，Shader 工具链仍独立管理。

调整原“统一 Granit 缓存根目录”的实施方式：Granit 附属源码缓存默认放在各构建目录
`_deps/granit-cache`，避免向只读共享源码写入，也不让未使用 Gneiss 并发锁的上游辅助函数
共享写入目录。显式上游缓存配置保持优先；本轮未修改 Granit 源码。

Windows Clang Debug 默认归档路径重建 Editor、assetc 及 CAORS 测试目标成功。
CAORS VFS/Application/material、assetc inspect/cook、隐藏 Editor 冒烟、Granit 版本缓存、
源码缓存测试共 8 项通过。两个独立 Debug/Release 全项目离线 configure 成功，共享源码，
构建目录隔离；Release 仅配置。逐文件比较 Granit（1873 文件）与 CAORS（812 文件）源码
缓存和归档，未发现改动或额外文件（缓存完成标记除外）。

两个依赖的本地源码覆盖配置成功；自定义 Granit 提交在离线且无本地覆盖时按预期拒绝。
主开发 preset 原有 CAORS 本地覆盖在默认归档测试后恢复。尚未验证 Linux、完整安装消费
矩阵和完整 Release 构建；也不宣称整个工程已支持离线或所有 Windows 长路径场景。
本任务完成源码获取迁移，不包含 CAORS 动态库切换与材质资源生产路径接入。
