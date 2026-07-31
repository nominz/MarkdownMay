# 第三阶段风险样机

这里不是最终产品代码，只用于验证冻结设计中的高风险技术点。正式代码在后续阶段进入 `src/`。

## 样机

- `app/`：Win32 + 系统 RichEdit，验证渲染编辑表面、中文、DPI 和两段启动；
- `scintilla_probe/`：静态编入 Scintilla 5.6.4，验证源码编辑表面；
- `core/markdown_probe.*`：静态 MD4C 0.5.3，验证 GFM 标题、表格和任务项；
- `core/image_probe.*`：WIC 本地图片探测及像素上限；
- `core/minimal_export.*`：不依赖 Office 的最小 PDF、DOCX 容器；
- `tests/`：接口、Markdown、图片和导出冒烟测试。

## 构建

在普通 PowerShell 或“开发人员 PowerShell”中执行：

```powershell
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug
cmake --build --preset windows-x64-release
ctest --test-dir build/windows-x64-debug -C Release --output-on-failure
```

Release 样机位置：

- `build/windows-x64-debug/prototype/app/Release/MarkdownMay-Prototype.exe`
- `build/windows-x64-debug/prototype/scintilla_probe/Release/MarkdownMay-Scintilla-Probe.exe`

详细结论见 `docs/第三阶段风险验证报告.md`。
