<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-193：0.10.0 后公共面增量审计

## 结论

0.10.0 至 0.29.0 的公共安装面新增 Game Module、结构化日志、窗口尺寸查询和 Prefab 作者操作。
Game Module、日志、窗口查询及 Prefab 作者接口继续标记为 Experimental；Editor–Runtime IPC、渲染
执行器和 Granit 异步 GPU 对象均未进入公共安装头。

审计发现稳定性清单遗漏 10 个已导出的 Experimental 声明：窗口尺寸查询，以及 9 个 Prefab 查询、
创建、刷新和编辑符号。此前门禁只比较两份人工维护的文本，无法发现公共头与清单之间的漂移。

## 分类结果

| 增量能力 | 分类 | 原因 |
| --- | --- | --- |
| Game Module 与 Game Context | Experimental | 已有真实工程使用，但动态加载和未来脚本边界仍会演进 |
| 结构化日志 | Experimental | 消息布局可用，Sink、过滤和跨模块策略尚未冻结 |
| Application 窗口尺寸查询 | Experimental | 当前主要服务 Editor 组合，尚未形成完整窗口公共契约 |
| Prefab 作者与刷新操作 | Experimental | 属于作者工具能力，不进入首批运行时 Stable 候选 |
| Editor–Runtime IPC | Internal | 进程间协议独立版本化，不作为 SDK 公共 C ABI |
| Render Executor 与 Granit 异步对象 | Internal | 后端和线程所有权由 Render Service 封装 |

## 验证结果

- 公共 C 头共声明 102 个 `GNEISS_API` 符号。
- 46 个为 Stable 候选，56 个以 `GNEISS_EXPERIMENTAL` 标记。
- ABI 基线、稳定性清单与公共 C 声明已经恢复全集一致。
- C++ 包装仍基于同一 C ABI，没有新增平行运行时状态。

后续由 M-194 将本次人工审计规则固化为自动门禁。
