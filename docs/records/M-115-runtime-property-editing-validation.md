<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-115：Runtime 属性编辑验收记录

## 结论

M-115 已完成。Lantern Gallery 已覆盖真实 Runtime 属性写入、暂停稳定性、恢复后的游戏逻辑更新和
新会话隔离；Windows Clang 与 VS2022 全矩阵，以及 Linux、Granit Runtime 与 Sanitizer 手动
Actions 均已通过。

## 示例闭环

- Runtime 与 Editor 完成属性编辑能力协商后，测试对场景根节点提交平移和旋转写入并等待成功确认。
- Lantern Gallery 游戏模块随后覆盖测试写入的旋转，验证运行逻辑与覆盖提示所依赖的真实行为。
- 暂停 Runtime 后提交缩放写入，等待权威镜像确认，并验证暂停期间值保持稳定。
- 恢复 Runtime 后验证 Lantern Gallery 游戏模块继续更新根节点旋转。
- 停止并启动第二个 Runtime 会话后，旧会话的属性命令状态不会泄漏到新会话。
- Editor 集成测试继续覆盖把 Runtime Transform 显式应用到作者场景、撤销、重做、脏状态和保存。
- MSVC 验收发现并修复属性值转换中的不可达代码警告，未降低警告等级。

## Windows 验证结果

| 配置 | 结果 |
| --- | --- |
| Clang Shared Debug | 全量构建通过，CTest 106/106 通过 |
| Clang Shared Release | 全量构建通过，CTest 106/106 通过 |
| Clang Static Debug | 全量构建通过，CTest 104/104 通过 |
| Clang Static Release | 全量构建通过，CTest 104/104 通过 |
| VS2022 Shared Debug | 全量构建通过，CTest 106/106 通过 |
| VS2022 Shared Release | 全量构建通过，CTest 106/106 通过 |
| VS2022 Static Debug | 全量构建通过，CTest 104/104 通过 |
| VS2022 Static Release | 全量构建通过，CTest 104/104 通过 |

## Linux 远端矩阵

[Linux Actions 运行 33624248768](https://github.com/synchronized/gneiss/actions/runs/33624248768)
基于候选提交 `f6dfaa6`，以下 7 项 Job 全部通过：

- GCC Core Shared 与 Static。
- Clang Core Shared 与 Static。
- Granit Runtime Shared 与 Static，包括无头窗口测试。
- Sanitizer Runtime，包括内存错误与退出泄漏检查。
