add_executable(
    MarkdownMayFileIoUnit
    file_service_test.cpp
    text_encoding_test.cpp
)

target_link_libraries(
    MarkdownMayFileIoUnit
    PRIVATE markdownmay_compiler_options markdownmay_core
)

add_test(NAME fileio_unit COMMAND MarkdownMayFileIoUnit)
