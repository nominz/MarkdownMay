add_executable(MarkdownMayCoreWorkflow core_workflow_test.cpp)
target_link_libraries(
    MarkdownMayCoreWorkflow
    PRIVATE markdownmay_compiler_options markdownmay_core
)
add_test(NAME core_workflow_integration COMMAND MarkdownMayCoreWorkflow)
