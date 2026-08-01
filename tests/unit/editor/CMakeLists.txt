add_executable(MarkdownMayEditorUnit paragraph_editor_test.cpp)
target_link_libraries(
    MarkdownMayEditorUnit
    PRIVATE markdownmay_compiler_options markdownmay_editor
)
add_test(NAME paragraph_editor_unit COMMAND MarkdownMayEditorUnit)

add_executable(MarkdownMayInlineFormatterUnit inline_formatter_test.cpp)
target_link_libraries(
    MarkdownMayInlineFormatterUnit
    PRIVATE markdownmay_compiler_options markdownmay_editor
)
add_test(NAME inline_formatter_unit COMMAND MarkdownMayInlineFormatterUnit)

add_executable(MarkdownMayBlockFormatterUnit block_formatter_test.cpp)
target_link_libraries(
    MarkdownMayBlockFormatterUnit
    PRIVATE markdownmay_compiler_options markdownmay_editor
)
add_test(NAME block_formatter_unit COMMAND MarkdownMayBlockFormatterUnit)

add_executable(MarkdownMayListEditorUnit list_editor_test.cpp)
target_link_libraries(
    MarkdownMayListEditorUnit
    PRIVATE markdownmay_compiler_options markdownmay_editor
)
add_test(NAME list_editor_unit COMMAND MarkdownMayListEditorUnit)

add_executable(MarkdownMayImageEditorUnit image_controller_test.cpp)
target_link_libraries(
    MarkdownMayImageEditorUnit
    PRIVATE markdownmay_compiler_options markdownmay_editor
)
add_test(NAME image_editor_unit COMMAND MarkdownMayImageEditorUnit)

add_executable(MarkdownMayTableEditorUnit table_editor_test.cpp)
target_link_libraries(
    MarkdownMayTableEditorUnit
    PRIVATE markdownmay_compiler_options markdownmay_editor
)
add_test(NAME table_editor_unit COMMAND MarkdownMayTableEditorUnit)
