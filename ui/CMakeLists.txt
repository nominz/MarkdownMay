add_library(markdownmay_ui STATIC
    document_window.cpp
    main_window.cpp
    menu_controller.cpp
    status_bar.cpp
    toolbar.cpp
)
target_include_directories(markdownmay_ui
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/app/include"
)
target_link_libraries(
    markdownmay_ui
    PUBLIC markdownmay_editor
    PRIVATE markdownmay_compiler_options comctl32 shell32 user32
)
