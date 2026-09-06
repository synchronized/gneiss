<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-206～M-212：0.32.0 构建配置与发布包管线验收记录

## 结果

Gneiss 0.32.0 已为工程定义 Debug、Development 与 Shipping 三种构建配置，并贯通命令行与
Editor 的配置、构建、文件收集、清单校验和 ZIP 归档工作流。旧版工程继续按 Debug 配置加载，
新工程模板显式声明各配置的 CMake preset 与模块输出目录。

发布目录通过同级临时目录事务生成，不覆盖已有输出。版本化清单记录工程、配置、平台、架构、入口、
文件大小和 SHA-256；校验器能够发现文件篡改、遗漏及额外文件。ZIP 使用稳定排序、固定时间与权限元数据，
相同输入能够产生相同哈希。Shipping 包排除 PDB，Development 包可保留相邻调试符号。

版本号已提升至 0.32.0；工程描述升级至格式 v4，未改变公共 C ABI、资产二进制格式或
Editor–Runtime IPC 协议。

## 本地验证

- Windows MSVC Shared Debug：完整构建及 132/132 测试通过。
- Windows Clang Shared Debug：严格警告构建及 132/132 测试通过。
- 安装 SDK 冒烟覆盖：外部工程 Debug 构建与启动、Development/Shipping 打包与包内启动通过。
- 清单成功校验，篡改检测通过；两次 Development ZIP 的 SHA-256 一致。
- Shipping 包不包含 PDB，生成的 ZIP 可由 CMake 归档读取器打开。
- `git diff --check` 通过。

## 远端验证

- [Linux Actions 34033652006](https://github.com/synchronized/gneiss/actions/runs/34033652006)：
  Clang/GCC Shared/Static、Granit Runtime Shared/Static 和 Sanitizer 共 7 个作业全部通过。
- [Windows Actions 34033654034](https://github.com/synchronized/gneiss/actions/runs/34033654034)：
  MSVC Runtime Shared/Static 与安装 Consumer Shared/Static 共 4 个作业全部通过。

完整远端矩阵针对候选代码仅执行一次；本记录与路线图状态更新不改变构建、依赖或测试结果，因此不重复
触发 Actions。
