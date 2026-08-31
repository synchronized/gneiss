<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-75：Runtime 进程失败与恢复验收记录

## 结论

Windows VS2022 Debug 构建已通过 Runtime 子进程失败、退出、停止和恢复矩阵。Editor 能保留合并输出、
退出码和每次会话日志路径；失败会话的日志文件在后续运行及 Editor 退出后继续保留。

## 环境

| 项目 | 值 |
| --- | --- |
| 操作系统 | Windows 10 x64 |
| 构建 | VS2022 Debug、Shared、Granit Fetch |
| 测试日期 | 2026-08-31 |

## 自动化矩阵

| 路径 | 验收结果 |
| --- | --- |
| 可执行文件不存在 | 返回 `not_found`，不进入运行状态 |
| 子进程非零退出 | 保留 stdout、stderr 和退出码 23 |
| 子进程异常终止 | 强制终止后报告非零退出码 |
| 重复启动 | 运行中再次启动返回 `invalid_state` |
| 终止后再次启动 | 同一控制器可重新启动并取得新退出码 |
| 所有者先退出 | 控制器析构时终止仍在运行的子进程 |
| 启动场景缺失 | Runtime 退出码为 2，输出包含 `startup_scene`、结果码和缺失路径 |
| Editor 正常停止 | 输出包含 `stop_request` 与 `shutdown`，退出码为 0 |
| Runtime 忽略停止请求 | 两秒后强制终止，输出明确记录超时 |
| 失败日志保留 | 失败日志文件在下一次运行后仍存在，Editor 显示当前日志路径 |

## 启动基线

`gneiss.editor.runtime-process` 在本机单次测得从创建进程到捕获 `first_frame` 事件为 235 ms。
该数据包含进程创建、Application 初始化、启动场景加载和进入首帧，仅用于记录当前数量级，不设置
性能门槛。

## 验证命令

```powershell
cmake --build --preset windows-vs2022-debug --target gneiss_child_process_test gneiss_editor_runtime_process_test gneiss_editor
ctest --test-dir build/windows-vs2022-debug -C Debug -R "gneiss\.(process\.child-process|editor\.(runtime-process|runtime-launch|smoke))" --output-on-failure
```

上述 4 项测试全部通过。POSIX 后端仍需在 Linux 环境执行同一行为测试，结果不在本记录中推断。
