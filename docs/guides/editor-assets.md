<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 在 Editor 中导入资产

## 适用场景

当前 Editor 可以把受支持的静态 glTF/GLB 导入工程，并在 Asset Browser 中查看源文件、派生产物
及导入状态。导入能力仅在 `GNEISS_BUILD_TOOLS=ON` 时构建。

## 导入资产

1. 打开工程后，在左下方 Asset Browser 中选择 `Import...`。
2. 选择 `.gltf` 或 `.glb` 文件。
3. Editor 将文件复制到工程 `sources/`，再把派生产物写入
   `assets/imported/<稳定源键>/`。
4. 成功后，源文件显示为 `Ready`，派生 Mesh、Material、Texture 和 Scene 显示为 `GEN`。

同名外部源不会覆盖既有文件，Editor 会追加数字后缀。引用外部缓冲或图片的 `.gltf` 必须保证其
依赖在复制后的 `sources/` 中仍可解析；否则导入会失败并保留该源文件供排查。GLB 或使用内嵌数据
的 glTF 不受此限制。

## 重新导入

1. 在 Asset Browser 中选择 `SRC` 项。
2. 选择 `Reimport`。
3. Editor 重新生成该源文件的独占派生目录，并在成功后更新资产索引。

导入失败不会覆盖该源文件上一份完整索引和派生产物。修改源内容、删除派生产物或删除源文件后，
选择 `Refresh` 会分别显示 `Stale` 或 `Missing`；未建立索引的源文件显示 `Untracked`。

## 自动更新与故障恢复

已导入源文件变化后，Editor 通过去抖和稳定读取检查自动重新导入。监听或候选队列丢事件后，
会按索引补查已导入源文件，包括删除；`Check indexed sources` 可手动请求检查，不自动导入
未登记的新文件。失败不会每帧无限重试，修复文件后可使用 `Reimport` 或再次请求检查。

监听启动或运行失败会在 Asset Browser 显示目录与错误，可在排除原因后使用 `Restart watch`。
缺失的可选 `sources/` 目录显示等待状态，目录创建后自动开始监听，无需手动建空目录。
累计丢失计数保留用于诊断；补扫完成不代表所有导入或 Runtime 更新都成功，应检查对应错误。

Scene/Prefab 作者文件可使用 `Check author assets` 补查；丢事件和重启监听后也会自动补查。
如果当前场景有未保存修改且受该文件影响，只提示冲突，不覆盖编辑内容。先保留本地修改或明确
放弃修改，再重新打开受影响场景或执行检查。

`Import succeeded` 表示派生资产写入成功。Runtime 更新另有修订状态：同步发布失败应重新导入
或保存相应资产；异步应用失败可在修复资产后选择 `Retry Runtime asset sync`，重新同步已知资产。
显示要求重启时，停止并重新启动 Runtime。尚未连接 Runtime 的修订会保留到下次启动。

## 后台任务与关闭

源文件补查、哈希、导入和浏览器刷新通过共享任务池的工程串行队列执行，不绑定固定线程。
作者 Scene/Prefab 枚举和哈希使用独立任务作用域。点击按钮后任务入队，浏览器
保留上一份快照，完成后更新；状态栏显示阶段、当前源文件和排队数量。解析器没有可靠的字节进度，
因此不显示百分比。连续导入合并刷新，避免每个产物都重复扫描整个工程。

`Cancel asset tasks` 清空待处理任务并撤销尚未提交的导入。解析调用不能强制中断，取消可能需要
等待当前阶段完成；已经显示 `Committing` 的任务会完成或回滚。取消外部导入可能保留已复制到
`sources/` 的源文件，之后可手动重新导入。失败项可使用 `Retry last import` 重试。

导入在 `.gneiss/import-work/` 暂存，并复核源文件哈希；过期任务丢弃产物后重排。普通导入错误不会
无限自动重试。索引保存失败会恢复旧派生目录；若文件系统同时阻止回滚，错误中会提示保留
`*.gneiss-index-backup` 备份，应在关闭 Editor 后恢复并排查磁盘或权限问题。

关闭 Editor 会停止接收任务、取消未提交工作并等待两个服务作用域收尾，然后释放共享线程池。
等待期间仍处理窗口帧。
打开 Export Package 对话框暂停新资产任务；当前任务结束后才允许导出，关闭对话框后恢复。

Scene/Prefab 文件检查在后台分批执行；冲突判断使用主线程消费结果时的当前脏状态和 Prefab 引用。
自身保存以实际写入字节建立基线，并使保存前的回执失效。文档加载和 Scene/ECS 应用仍在主线程，
大量作者文件一起变化仍可能造成应用停顿；本版不承诺所有工程 I/O 都已后台化。
Windows Debug 构建中，大块解析分配仍可能通过调试运行库的共享堆锁造成短暂停顿；后台化不等于
Debug 的全部帧时限保证。实际 Release 与 Debug 数据见
[0.39 验收记录](../records/M-250-255-0.39.0-validation.md)。

外部依赖文件尚未纳入源文件哈希追踪。提交不是跨进程读隔离或断电恢复事务，请勿同时用其他进程
修改相同工程的索引或派生产物。

## 放入场景

- 选择导入产生的 `.gneiss-mesh` 后使用 `Add Mesh`，Editor 会自动配对同一导入目录中的首个
  Material，并创建新的根场景节点。
- 选中已有 Mesh Renderer 节点后，在 Asset Browser 选择 Mesh 或 Material，再使用
  `Apply to Node` 替换对应引用。
- 成功操作会把场景标记为已修改；使用 `Save` 或 `Ctrl+S` 后，重新加载工程仍保留节点和引用。

资产放置通过 Scene Instance 作者编辑 API 完成，不直接保存 RID，也不建立 Editor 私有场景状态。

## 撤销与重做

- 使用 `Ctrl+Z` 撤销最近一次属性修改、Mesh Renderer 节点创建、资源替换或节点删除。
- 使用 `Ctrl+Shift+Z` 重做；也可以通过 `Edit` 菜单执行并查看当前是否可用。
- 执行新的编辑后会清空 redo。失败的编辑和资产导入不会进入命令历史。
- 同一次属性拖动产生的连续值会合并为一个命令；下一次独立拖动仍会建立新的 Undo 步骤。
- 成功保存会建立保存点；Undo/Redo 回到保存点时场景恢复为未修改状态，离开保存点后重新标记修改。
- 默认最多保留 256 个命令；超出后从最旧命令开始淘汰。Undo/Redo 执行失败时历史位置不变。
- `Delete Selected` 当前删除选中的无子 Mesh Renderer 节点；有子节点或非 Mesh Renderer 节点不会
  被隐式删除。

命令只保存节点 UUID、父 UUID、属性值和资产 URI，不保存 Entity ID、Scene Node ID、组件地址、
ImGui 状态或资源 RID。节点被恢复后，后续命令会通过 UUID 重新解析新的运行时句柄。

## Runtime 场景加载与切换

启动 Runtime 后，Asset Browser 显示启动场景的当前阶段与阶段计数。选择工程内已保存的
`.scene.json`，使用 `Load scene in Runtime` 完整切换 Runtime 场景；准备期间旧场景继续运行。
使用 `Cancel scene load` 请求取消，等待终态后可用 `Retry scene load` 重试失败或取消的请求。

完整切换会建立新的 Runtime 对象身份，并使旧检查镜像、选择和属性请求失效；它不修改 Editor
作者场景或撤销历史。未保存编辑应先保存。外部 Scene/Prefab 文件修订仍走原有结构热重载。
只有双方协商支持场景加载协议时才启用切换操作；流程与线程边界见
[任务调度](task-scheduling.md#runtime-完整场景加载)。

## 当前限制

- 只支持现有 glTF 导入器覆盖的静态 Mesh、Material、PNG Texture 和 Scene 范围。
- Windows 提供系统文件选择器；其他平台当前需要等待原生文件选择器接入。
- 导入与重新导入是工程资产操作，不进入场景 Undo/Redo 历史。
