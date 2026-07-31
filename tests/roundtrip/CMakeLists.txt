add_executable(MarkdownMayRoundTrip markdown_roundtrip_test.cpp)
target_link_libraries(
    MarkdownMayRoundTrip
    PRIVATE markdownmay_compiler_options markdownmay_core
)
add_test(NAME markdown_roundtrip COMMAND MarkdownMayRoundTrip)
