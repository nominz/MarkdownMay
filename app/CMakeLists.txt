add_library(markdownmay_app_shell STATIC
    application.cpp command_dispatcher.cpp command_line.cpp startup.cpp)
target_include_directories(markdownmay_app_shell PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
    markdownmay_app_shell
    PUBLIC markdownmay_ui markdownmay_platform
    PRIVATE markdownmay_compiler_options comdlg32 ole32 user32
)

add_executable(MarkdownMay WIN32 main.cpp)
target_link_libraries(
    MarkdownMay
    PRIVATE markdownmay_compiler_options markdownmay_app_shell ole32 shell32 user32
)
set_target_properties(MarkdownMay PROPERTIES OUTPUT_NAME "MarkdownMay")
if(MSVC)
    target_link_options(MarkdownMay PRIVATE
        /DYNAMICBASE /NXCOMPAT /GUARD:CF
        /MANIFEST:EMBED
        "/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/resources/MarkdownMay.application.manifest"
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
endif()
