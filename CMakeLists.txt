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

option(MARKDOWNMAY_BUILD_PROTOTYPE "Build the stage-3 risk prototype" ON)
option(MARKDOWNMAY_BUILD_TESTS "Build design contract smoke tests" ON)

if(MARKDOWNMAY_BUILD_PROTOTYPE)
    add_subdirectory(prototype)
endif()

if(MARKDOWNMAY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
