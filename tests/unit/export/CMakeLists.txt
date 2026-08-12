add_executable(
    MarkdownMayExportUnit
    export_document_test.cpp
    export_task_test.cpp
    docx_writer_test.cpp
    pdf_writer_test.cpp
    txt_writer_test.cpp
)
target_link_libraries(
    MarkdownMayExportUnit
    PRIVATE markdownmay_compiler_options markdownmay_core
)
add_test(NAME export_document_unit COMMAND MarkdownMayExportUnit)
