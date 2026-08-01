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
