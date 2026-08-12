add_executable(
    MarkdownMayContractSmoke
    contract_smoke.cpp
)

target_include_directories(
    MarkdownMayContractSmoke
    PRIVATE
        "${PROJECT_SOURCE_DIR}/design/include"
)

target_link_libraries(
    MarkdownMayContractSmoke
    PRIVATE
        markdownmay_compiler_options
)

add_test(NAME contract_smoke COMMAND MarkdownMayContractSmoke)

add_executable(
    MarkdownMayMarkdownProbeSmoke
    markdown_probe_smoke.cpp
)

target_link_libraries(
    MarkdownMayMarkdownProbeSmoke
    PRIVATE
        markdownmay_compiler_options
        markdownmay_prototype_core
)

add_test(NAME markdown_probe_smoke COMMAND MarkdownMayMarkdownProbeSmoke)

add_executable(
    MarkdownMayMinimalExportSmoke
    minimal_export_smoke.cpp
)

target_link_libraries(
    MarkdownMayMinimalExportSmoke
    PRIVATE
        markdownmay_compiler_options
        markdownmay_prototype_core
)

add_test(NAME minimal_export_smoke COMMAND MarkdownMayMinimalExportSmoke)

add_executable(
    MarkdownMayImageProbeSmoke
    image_probe_smoke.cpp
)

target_link_libraries(
    MarkdownMayImageProbeSmoke
    PRIVATE
        markdownmay_compiler_options
        markdownmay_prototype_core
        ole32
)

add_test(NAME image_probe_smoke COMMAND MarkdownMayImageProbeSmoke)

add_executable(
    MarkdownMayHtmlExportProbe
    html_export_probe.cpp
)

target_link_libraries(
    MarkdownMayHtmlExportProbe
    PRIVATE
        markdownmay_compiler_options
        markdownmay_prototype_core
)

add_test(NAME html_export_probe COMMAND MarkdownMayHtmlExportProbe)

add_subdirectory(unit/fileio)
add_subdirectory(unit/document)
add_subdirectory(roundtrip)
add_subdirectory(unit/services)
add_subdirectory(unit/image)
add_subdirectory(unit/editor)
add_subdirectory(unit/export)
add_subdirectory(integration)
