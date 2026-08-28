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
- `Delete Selected` 当前删除选中的无子 Mesh Renderer 节点；有子节点或非 Mesh Renderer 节点不会
  被隐式删除。

命令只保存节点 UUID、父 UUID、属性值和资产 URI，不保存 Entity ID、Scene Node ID、组件地址、
ImGui 状态或资源 RID。节点被恢复后，后续命令会通过 UUID 重新解析新的运行时句柄。

## 当前限制

- 只支持现有 glTF 导入器覆盖的静态 Mesh、Material、PNG Texture 和 Scene 范围。
- Windows 提供系统文件选择器；其他平台当前需要等待原生文件选择器接入。
- 导入与重新导入是工程资产操作，不进入场景 Undo/Redo 历史。
