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
