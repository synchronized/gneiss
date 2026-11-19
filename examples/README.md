<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 示例入口

只维护两个可执行示例：

- `gneiss_minimal_example`：无窗口初始化、创建场景与对象、更新三帧后退出，见 [main.cpp](minimal/main.cpp)。
- `gneiss_scene_viewer`：异步加载、相机控制、运行与关闭，默认显示随仓库提供的神庙。

```powershell
./build/windows-clang-debug/bin/gneiss_scene_viewer.exe
./build/windows-clang-debug/bin/gneiss_scene_viewer.exe --smoke
./build/windows-clang-debug/bin/gneiss_scene_viewer.exe --dual-view --smoke
./build/windows-clang-debug/bin/gneiss_scene_viewer.exe --assets build/windows-clang-debug/examples/lantern_gallery/assets --scene asset://scenes/gallery.scene.json
```

A/D 环绕相机，Esc 退出。场景须指定主相机；输入映射默认 `asset://input/default.input-map.json`，
可通过 `--input URI` 指定，须包含 `move_horizontal`、`quit` 动作。当前环绕半径为 6、高度为 2.3。
`--assets 路径` 指定资产根，`--scene URI` 指定场景；无需修改相机 UUID。
默认先查可执行文件相邻安装树 `share/gneiss/examples/scene-viewer/assets`，再使用构建时的源码资产根。

`--smoke` 加载完成后运行三帧；`--measure` 预热 60 帧、采样 300 帧并输出启动、运行和关闭耗时 JSON。随后输出独立的 render_queue JSON，
记录等待队列峰值、命令/帧输入字节峰值、拒绝/替换次数和最大排队时间；这不是显存统计。
两者互斥。测量值是宿主更新间隔，不是 GPU 时长，不作为跨机器性能门槛。
`--dual-view` 将同一 World 显示为左右两个离屏视图，左侧跟随主相机，右侧固定独立相机。
与 `--smoke` 一起使用时，两轮各运行三帧，目标从 320×240/320×384 更换为
512×240/512×384，每轮回读窗口合成图，检查两侧图像有变化且不同，再释放目标。
这是 GPU 功能验收；压力恢复与默认预算测量仍按 PLAN-115 推进。

`temple/assets` 是神庙数据，来源及许可见 [资产说明](temple/assets/ASSET_ORIGINS.md)。
`lantern_gallery` 保留 GLB 导入和编辑工作流夹具；`editor_demo` 保留安装项目数据，均不是额外可执行示例。
版本与属性检查已迁入 tests，不再安装独立示例程序。所有 GPU 冒烟测试均带 gpu 标签。
