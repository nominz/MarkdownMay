# 马冬梅（Markdown May）

马冬梅是一个面向普通 Windows 办公用户的轻量 Markdown 记事本。

项目目标：

- 双击 Markdown 文件即可快速打开；
- 使用单一所见即所得界面阅读和编辑；
- 支持标题、列表、表格、链接、代码和图片；
- 支持导出 PDF 和 Word（DOCX）；
- 默认中文，无账号、无广告、无云服务、无插件配置；
- 以单个 EXE 发布，可注册为 Markdown 文件打开程序；
- 免费、开源，采用 MIT 许可证。

## 当前状态

项目处于瀑布开发的第一阶段：需求冻结。此时不实现产品代码，先完成并评审软件需求规格说明书。

- [产品与技术方案](docs/MarkdownMay-产品与技术方案.md)
- [软件需求规格说明书](docs/软件需求规格说明书.md)

## 计划技术栈

- C++20
- Win32 API
- Windows RichEdit / Text Object Model
- MD4C
- DirectWrite / Direct2D / Windows Imaging Component
- CMake + MSVC

## 构建

需求冻结阶段尚无可构建产品。详细设计完成后再提交正式构建目标，避免由临时代码决定架构。

## 许可证

[MIT](LICENSE) © 2026 nominz

