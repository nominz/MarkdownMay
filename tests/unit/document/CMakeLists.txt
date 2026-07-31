add_executable(
    MarkdownMayDocumentUnit
    document_parser_test.cpp
    document_session_test.cpp
)
target_link_libraries(
    MarkdownMayDocumentUnit
    PRIVATE markdownmay_compiler_options markdownmay_core
)
add_test(NAME document_parser_unit COMMAND MarkdownMayDocumentUnit)
