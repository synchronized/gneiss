<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Editor 字体资源

`Inter-Regular.ttf` 来自 Inter 4.1，原作者为 The Inter Project Authors：

- 上游项目：https://github.com/rsms/inter
- 发布包：https://github.com/rsms/inter/releases/tag/v4.1
- 文件 SHA-256：`40d692fce188e4471e2b3cba937be967878f631ad3ebbbdcd587687c7ebe0c82`
- 许可：SIL Open Font License 1.1，完整文本见同目录 `LICENSE.txt`

该字体仅用于 Gneiss Editor 界面，不属于 Runtime 公共接口或用户工程资产。

中文回退字体使用 Noto Sans SC 可变 TTF，由 Editor 构建按固定 Google Fonts 提交下载，不复制到
源码仓库：

- 上游项目：https://github.com/google/fonts/tree/main/ofl/notosanssc
- 固定提交：`45b0855d499c093e4d1bd08926fec4e1a582e225`
- 文件 SHA-256：`a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da`
- 许可：SIL Open Font License 1.1
- 字体版权：Copyright 2014-2021 Adobe，保留字体名称 `Source`

ImGui 运行时仍以 Inter 作为默认字体，只把常用简体中文字形从 Noto Sans SC 合并进字体图集；不修改
下载的字体文件，也不把字体类型或路径暴露到公共接口。
