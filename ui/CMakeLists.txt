add_library(markdownmay_ui STATIC
    document_window.cpp
    main_window.cpp
)
target_include_directories(markdownmay_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
    markdownmay_ui
    PUBLIC markdownmay_editor
    PRIVATE markdownmay_compiler_options user32
)
