add_executable(MarkdownMayImageStoreUnit image_store_test.cpp)
target_link_libraries(
    MarkdownMayImageStoreUnit
    PRIVATE markdownmay_compiler_options markdownmay_core
)
add_test(NAME image_store_unit COMMAND MarkdownMayImageStoreUnit)
