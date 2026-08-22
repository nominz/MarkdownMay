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

add_executable(MarkdownMayInputControllerUnit input_controller_test.cpp)
target_link_libraries(
    MarkdownMayInputControllerUnit
    PRIVATE markdownmay_compiler_options markdownmay_editor
)
add_test(NAME input_controller_unit COMMAND MarkdownMayInputControllerUnit)

add_executable(MarkdownMayFullProjectionUnit full_projection_test.cpp)
target_compile_definitions(
    MarkdownMayFullProjectionUnit
    PRIVATE MARKDOWNMAY_COMPLETE_FIXTURE=L"${PROJECT_SOURCE_DIR}/tests/fixtures/complete_markdown.md"
)
target_link_libraries(
    MarkdownMayFullProjectionUnit
    PRIVATE markdownmay_compiler_options markdownmay_editor
)
add_test(NAME full_projection_unit COMMAND MarkdownMayFullProjectionUnit)

add_executable(MarkdownMaySourceSyncUnit source_sync_test.cpp)
target_link_libraries(
    MarkdownMaySourceSyncUnit
    PRIVATE markdownmay_compiler_options markdownmay_editor
)
add_test(NAME source_sync_unit COMMAND MarkdownMaySourceSyncUnit)

add_executable(MarkdownMayHeadingFoldUnit heading_fold_controller_test.cpp)
target_link_libraries(
    MarkdownMayHeadingFoldUnit
    PRIVATE markdownmay_compiler_options markdownmay_editor
)
add_test(NAME heading_fold_controller_unit COMMAND MarkdownMayHeadingFoldUnit)

add_executable(MarkdownMayBlockInteractionUnit block_interaction_controller_test.cpp)
target_link_libraries(
    MarkdownMayBlockInteractionUnit
    PRIVATE markdownmay_compiler_options markdownmay_editor
)
add_test(NAME block_interaction_controller_unit COMMAND MarkdownMayBlockInteractionUnit)

add_executable(MarkdownMayRenderStyleUnit render_style_test.cpp)
target_link_libraries(MarkdownMayRenderStyleUnit PRIVATE markdownmay_compiler_options markdownmay_editor)
add_test(NAME render_style_unit COMMAND MarkdownMayRenderStyleUnit)
