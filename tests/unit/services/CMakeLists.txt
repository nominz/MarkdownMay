add_executable(MarkdownMayServicesUnit local_services_test.cpp)
target_link_libraries(
    MarkdownMayServicesUnit
    PRIVATE markdownmay_compiler_options markdownmay_core
)
add_test(NAME services_unit COMMAND MarkdownMayServicesUnit)

add_executable(MarkdownMayDocumentSplitUnit document_split_test.cpp)
target_link_libraries(
    MarkdownMayDocumentSplitUnit
    PRIVATE markdownmay_compiler_options markdownmay_core
)
add_test(NAME document_split_unit COMMAND MarkdownMayDocumentSplitUnit)
