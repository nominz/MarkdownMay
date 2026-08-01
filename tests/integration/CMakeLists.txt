add_executable(MarkdownMayCoreWorkflow core_workflow_test.cpp)
target_link_libraries(
    MarkdownMayCoreWorkflow
    PRIVATE markdownmay_compiler_options markdownmay_core
)
add_test(NAME core_workflow_integration COMMAND MarkdownMayCoreWorkflow)

add_executable(MarkdownMayWysiwygParagraph WIN32 wysiwyg_paragraph_test.cpp)
target_link_libraries(
    MarkdownMayWysiwygParagraph
    PRIVATE markdownmay_compiler_options markdownmay_editor user32
)
add_test(NAME wysiwyg_paragraph_integration COMMAND MarkdownMayWysiwygParagraph)

add_executable(MarkdownMayWysiwygInline WIN32 wysiwyg_inline_test.cpp)
target_link_libraries(
    MarkdownMayWysiwygInline
    PRIVATE markdownmay_compiler_options markdownmay_editor user32
)
add_test(NAME wysiwyg_inline_integration COMMAND MarkdownMayWysiwygInline)

add_executable(MarkdownMayWysiwygBlock WIN32 wysiwyg_block_test.cpp)
target_link_libraries(
    MarkdownMayWysiwygBlock
    PRIVATE markdownmay_compiler_options markdownmay_editor user32
)
add_test(NAME wysiwyg_block_integration COMMAND MarkdownMayWysiwygBlock)

add_executable(MarkdownMayWysiwygList WIN32 wysiwyg_list_test.cpp)
target_link_libraries(
    MarkdownMayWysiwygList
    PRIVATE markdownmay_compiler_options markdownmay_editor user32
)
add_test(NAME wysiwyg_list_integration COMMAND MarkdownMayWysiwygList)

add_executable(MarkdownMayWysiwygImage WIN32 wysiwyg_image_test.cpp)
target_link_libraries(
    MarkdownMayWysiwygImage
    PRIVATE markdownmay_compiler_options markdownmay_editor user32
)
add_test(NAME wysiwyg_image_integration COMMAND MarkdownMayWysiwygImage)

add_executable(MarkdownMayWysiwygTable WIN32 wysiwyg_table_test.cpp)
target_link_libraries(
    MarkdownMayWysiwygTable
    PRIVATE markdownmay_compiler_options markdownmay_editor user32
)
add_test(NAME wysiwyg_table_integration COMMAND MarkdownMayWysiwygTable)

add_executable(MarkdownMayWysiwygInput WIN32 wysiwyg_input_test.cpp)
target_link_libraries(
    MarkdownMayWysiwygInput
    PRIVATE markdownmay_compiler_options markdownmay_editor user32
)
add_test(NAME wysiwyg_input_integration COMMAND MarkdownMayWysiwygInput)

add_executable(MarkdownMayWysiwygComplete WIN32 wysiwyg_complete_test.cpp)
target_compile_definitions(
    MarkdownMayWysiwygComplete
    PRIVATE MARKDOWNMAY_COMPLETE_FIXTURE=L"${PROJECT_SOURCE_DIR}/tests/fixtures/complete_markdown.md"
)
target_link_libraries(
    MarkdownMayWysiwygComplete
    PRIVATE markdownmay_compiler_options markdownmay_editor user32
)
add_test(NAME wysiwyg_complete_integration COMMAND MarkdownMayWysiwygComplete)

add_executable(MarkdownMaySourceView WIN32 source_view_test.cpp)
target_link_libraries(
    MarkdownMaySourceView
    PRIVATE markdownmay_compiler_options markdownmay_editor scintilla_vendor user32
)
add_test(NAME source_view_integration COMMAND MarkdownMaySourceView)

add_executable(MarkdownMaySplitView WIN32 split_view_test.cpp)
target_link_libraries(
    MarkdownMaySplitView
    PRIVATE markdownmay_compiler_options markdownmay_editor scintilla_vendor user32
)
add_test(NAME split_view_integration COMMAND MarkdownMaySplitView)

add_executable(MarkdownMayViewModes WIN32 view_modes_test.cpp)
target_link_libraries(
    MarkdownMayViewModes
    PRIVATE markdownmay_compiler_options markdownmay_editor scintilla_vendor user32
)
add_test(NAME view_modes_integration COMMAND MarkdownMayViewModes)

add_executable(MarkdownMaySaveReopen WIN32 save_reopen_test.cpp)
target_link_libraries(
    MarkdownMaySaveReopen
    PRIVATE markdownmay_compiler_options markdownmay_editor scintilla_vendor user32
)
add_test(NAME save_reopen_integration COMMAND MarkdownMaySaveReopen)

add_executable(MarkdownMayAppShell WIN32 app_shell_test.cpp)
target_link_libraries(
    MarkdownMayAppShell
    PRIVATE markdownmay_compiler_options markdownmay_app_shell comctl32 user32 ole32
)
add_test(NAME app_shell_integration COMMAND MarkdownMayAppShell)
