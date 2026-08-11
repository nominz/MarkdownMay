# resources

存放编译进 EXE 的图标、菜单、字符串表、版本信息和应用清单。

- `MarkdownMay.rc`：正式 Win32 资源入口；由 `MarkdownMay` 目标固定编译。
- `resource.h`：稳定资源 ID。
- `icons/MarkdownMay.ico`：应用、窗口、任务栏和文件关联使用的正式图标源；构建时嵌入单 EXE，不作为旁置运行时文件发布。
