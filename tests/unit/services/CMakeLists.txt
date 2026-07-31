add_executable(MarkdownMayServicesUnit local_services_test.cpp)
target_link_libraries(
    MarkdownMayServicesUnit
    PRIVATE markdownmay_compiler_options markdownmay_core
)
add_test(NAME services_unit COMMAND MarkdownMayServicesUnit)
