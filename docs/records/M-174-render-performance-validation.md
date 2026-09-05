<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-174：0.26.0 渲染性能与跨平台验收

## 结果

0.26.0 已完成 Windows Clang Shared/Static 本地构建、全量自动化测试和 Lantern Gallery 冒烟
验证。压力夹具确认普通 Frame Packet 不再复制稳定 Texture 像素正文；Linux、MSVC、安装消费、
Granit Runtime 和 Sanitizer 仍需在最终提交上通过手动 Actions 后才能完成版本验收。

## 测量方法与复制量对比

- 环境：Windows x64、Clang Debug；测试使用 `gneiss.render_frame_packet` 的确定性 64×64 RGBA
  Texture 夹具。
- 0.25.0 行为基线：Frame Packet 按值复制资源快照，单张夹具 Texture 每帧至少复制 16,384
  字节像素正文，Mesh 顶点、索引和 Material 正文还会继续增加复制量。
- 0.26.0 结果：Frame Packet 只复制不可变资源共享引用；测试要求统计复制量严格小于 16,384
  字节，并校验捕获前后的 Mesh、Material 与 Texture 对象身份一致。
- 当前指标覆盖 Packet 构造与复制字节、队列等待、资源准备、Acquire、录制提交、Present、渲染
  线程总耗时、队列高水位、替换、拒绝和构造前节流。时间指标受 Debug 构建和本机负载影响，
  本记录不把单次耗时作为跨版本性能承诺。

## 本地验证

- `windows-clang-debug`：全量 131/131 通过。
- `windows-clang-static-debug`：全量 129/129 通过。
- Static 首轮全量测试的 `gneiss.runtime.stop-protocol` 曾在 6.72 秒处超时；该测试随后连续三次
  独立通过，完整 Static 套件复验 129/129 通过，暂按调度抖动记录而不隐藏。
- Shared/Static 的 `gneiss.render_frame_packet` 在扩大 Texture 压力夹具后再次通过。
- Lantern Gallery Debug 冒烟与 Profile 启动通过：Application 初始化 95.7535 ms、场景与资产
  74.5731 ms、输入 0.7426 ms、运行 0.797 ms。该数据仅证明诊断路径可用，不作为 Release 性能
  基线。
- `git diff --check` 通过。

## 待完成验证

- Linux Clang/GCC Shared/Static、Granit Runtime Shared/Static 和 Sanitizer。
- Windows MSVC Runtime Shared/Static 与安装后 C/C++ Consumer Shared/Static。

以上项目需要推送最终分支并手动触发远端 Actions；通过后再补充运行链接并将 M-174 标记为完成。
