add_library(markdownmay_editor STATIC
    block_formatter.cpp
    clipboard_controller.cpp
    find_replace_controller.cpp
    image_controller.cpp
    image_object.cpp
    inline_formatter.cpp
    list_editor.cpp
    paragraph_editor.cpp
    rich_projection.cpp
    richedit_host.cpp
    source_sync.cpp
    source_view.cpp
    split_view.cpp
    table_editor.cpp
    view_mode_controller.cpp
)
target_include_directories(markdownmay_editor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
    markdownmay_editor
    PUBLIC markdownmay_core
    PRIVATE markdownmay_compiler_options scintilla_vendor advapi32 d2d1 dwrite gdi32
        imm32 user32 ole32 oleaut32 windowscodecs shlwapi
)
