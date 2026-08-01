cmake_minimum_required(VERSION 3.25)

project(
    MarkdownMay
    VERSION 0.1.0
    DESCRIPTION "A lightweight WYSIWYG Markdown notepad for Windows"
    LANGUAGES C CXX
)

if(MSVC)
    set(
        CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerOptions)

add_library(
    md4c_vendor
    STATIC
    "${PROJECT_SOURCE_DIR}/third_party/md4c/src/md4c.c"
    "${PROJECT_SOURCE_DIR}/third_party/md4c/src/entity.c"
)
target_include_directories(md4c_vendor PUBLIC "${PROJECT_SOURCE_DIR}/third_party/md4c/src")
if(MSVC)
    target_compile_options(md4c_vendor PRIVATE /W3)
    target_compile_definitions(md4c_vendor PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

option(MARKDOWNMAY_BUILD_PROTOTYPE "Build the stage-3 risk prototype" ON)
option(MARKDOWNMAY_BUILD_TESTS "Build design contract smoke tests" ON)

add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/scintilla-5.6.4" scintilla)
add_subdirectory(src)
add_subdirectory(editor)

if(MARKDOWNMAY_BUILD_PROTOTYPE)
    add_subdirectory(prototype)
endif()

if(MARKDOWNMAY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
