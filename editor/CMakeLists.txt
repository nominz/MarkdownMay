add_library(markdownmay_editor STATIC
    block_formatter.cpp
    image_controller.cpp
    image_object.cpp
    inline_formatter.cpp
    list_editor.cpp
    paragraph_editor.cpp
    rich_projection.cpp
    richedit_host.cpp
    table_editor.cpp
)
target_include_directories(markdownmay_editor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
    markdownmay_editor
    PUBLIC markdownmay_core
    PRIVATE markdownmay_compiler_options user32 ole32 windowscodecs shlwapi
)
