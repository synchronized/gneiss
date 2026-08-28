<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 稳定运行时代表性 Consumer

本目录是独立的 CMake Consumer，只通过 `find_package(gneiss CONFIG REQUIRED)` 和
`gneiss::gneiss` 使用安装后的 Gneiss SDK。源码只包含公开头文件；运行资产复用 Temple 的公开
场景、输入、Mesh、Material 与 Texture 格式，并在配置时复制到独立构建目录。

先安装启用 Granit 平台适配的 Gneiss 及其依赖，再执行：

```powershell
cmake -S examples/stable_runtime -B build/stable-runtime-consumer `
  -DCMAKE_PREFIX_PATH=build/gneiss-install
cmake --build build/stable-runtime-consumer
ctest --test-dir build/stable-runtime-consumer --output-on-failure
```

交互运行时使用 `A`/`D` 绕场景旋转，按 `Esc` 退出。自动验证使用 `--smoke` 固定运行三帧。
`--measure` 固定预热 60 帧、采样 300 帧，并向标准输出写出一行 JSON；该模式用于 M-69 重复采样，
单次结果不作为性能门槛。

正式采样使用仓库的 `tools/performance/measure_stable_runtime.py`，显式记录编译器、构建类型以及
Gneiss/Granit 提交。默认先运行 1 个预热进程，再保存 10 个有效样本及汇总：

```powershell
python tools/performance/measure_stable_runtime.py `
  build/windows-clang-release/bin/gneiss_stable_runtime_example.exe `
  --output build/performance/stable-runtime.json --compiler "Clang 20" `
  --build-type Release --cpu "<CPU 型号>" --gpu "<GPU 型号>" --gpu-driver "<驱动版本>" `
  --gneiss-revision <提交> --granit-revision <提交>
```
