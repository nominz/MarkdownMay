# 马冬梅（Markdown May）

马冬梅是一个面向普通 Windows 办公用户的轻量 Markdown 记事本。

项目目标：

- 双击 Markdown 文件即可快速打开；
- 默认使用所见即所得渲染模式，同时提供源码模式和左右对照模式；
- 支持标题、列表、表格、链接、代码和图片；
- 支持导出 PDF 和 Word（DOCX）；
- 默认中文，无账号、无广告、无云服务、无插件配置；
- 以单个 EXE 发布，可注册为 Markdown 文件打开程序；
- 免费、开源，采用 MIT 许可证。

## 当前状态

项目已完成需求冻结，正在完成瀑布开发第二阶段的总体和详细设计。当前仍不实现产品代码，先冻结模块、数据、接口和错误契约。

- [产品与技术方案](docs/MarkdownMay-产品与技术方案.md)
- [需求规格说明书 V02（当前评审稿）](docs/需求规格说明书_V02.md)
- [需求规格说明书 V01（历史版本）](docs/需求规格说明书_V01.md)
- [功能模块与变更影响矩阵](docs/功能模块与变更影响矩阵.md)
- [总体设计说明书](docs/总体设计说明书.md)
- [详细设计说明书](docs/详细设计说明书.md)
- [接口头文件草案说明](docs/接口头文件草案.md)
- [数据结构图](docs/数据结构图.md)
- [错误码表](docs/错误码表.md)

## 计划技术栈

- C++20
- Win32 API
- Windows RichEdit / Text Object Model
- MD4C
- DirectWrite / Direct2D / Windows Imaging Component
- CMake + MSVC

## 构建

设计阶段尚无可构建产品。风险样机阶段再提交正式构建目标，避免由临时代码决定架构。

## 许可证

[MIT](LICENSE) © 2026 nominz
