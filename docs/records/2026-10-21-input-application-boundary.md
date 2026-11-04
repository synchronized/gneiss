<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 输入资源请求与 Application 采用边界

## 变更

继续沿用项目 input_map 引用独立输入配置，不引入全局 locator 或第二份设置文件。
资产管理器不再持有输入待采用状态，也不接收应用映射的回调。

新增资产层 action_map_request 原生接口，CAORS 实现在资产层适配；请求只允许成功领取一次，
输出为独立映射副本，不操作 InputService。Application 持有 action_map_application，
处理新请求替换、取消、采用及结果状态，加载成功后直接调用 InputService。
Application 与待采用类不包含 CAORS 类型；输入目录只保留数据、验证、类型 Loader 和服务。

资产工作队列的推进与采用分开；退出先取消待采用请求，再排空资产工作。
未绑定执行器时异步入口仍返回 not_ready，同步入口仍可使用。

## 验证

- Windows Clang Debug 六项通过：CAORS Application、asset-probe、VFS bridge，
  Runtime 异步输入启动，C Application 和 C++ input。
- Probe 保留旧采用测试：请求替换、取消、晚完成不覆盖新映射、失败保留旧动作 ID、一次采用。
- 新增原生请求验证：成功领取一次、再次领取拒绝且输出不变、取消终态。
- 原生无线程静态 Engine 与 Emscripten Node/MEMFS Application 测试分别通过。

## 剩余范围

同步结果发布进 CAORS 缓存以及旧请求代次协调尚未实现。
尚未把全部资源类型整合到一个 CAORS database；不为此预先增加通用类型擦除请求框架。
本次未验证浏览器网络 I/O，也未运行远端 Actions。
