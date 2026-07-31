add_executable(MarkdownMayEditorUnit paragraph_editor_test.cpp)
target_link_libraries(
    MarkdownMayEditorUnit
    PRIVATE markdownmay_compiler_options markdownmay_editor
)
add_test(NAME paragraph_editor_unit COMMAND MarkdownMayEditorUnit)
