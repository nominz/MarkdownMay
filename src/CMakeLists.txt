add_library(
    markdownmay_core
    STATIC
    fileio/file_service.cpp
    fileio/image_store.cpp
    fileio/line_endings.cpp
    fileio/path_utils.cpp
    fileio/text_encoding.cpp
    document/document.cpp
    document/document_reconciler.cpp
    document/document_session.cpp
    export/export_document.cpp
    export/docx_writer.cpp
    export/export_task.cpp
    export/pdf_writer.cpp
    export/txt_writer.cpp
    markdown/markdown_parser.cpp
    markdown/markdown_writer.cpp
    services/local_services.cpp
)

target_include_directories(
    markdownmay_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(markdownmay_core PRIVATE markdownmay_compiler_options md4c_vendor fontsub gdi32 ole32 windowscodecs)
