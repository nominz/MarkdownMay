add_library(
    markdownmay_core
    STATIC
    fileio/file_service.cpp
    fileio/line_endings.cpp
    fileio/path_utils.cpp
    fileio/text_encoding.cpp
)

target_include_directories(
    markdownmay_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(
    markdownmay_core
    PRIVATE markdownmay_compiler_options
)
