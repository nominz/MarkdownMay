add_library(
    md4c_vendor
    STATIC
    "${PROJECT_SOURCE_DIR}/third_party/md4c/src/md4c.c"
    "${PROJECT_SOURCE_DIR}/third_party/md4c/src/entity.c"
)

target_include_directories(
    md4c_vendor
    PUBLIC
        "${PROJECT_SOURCE_DIR}/third_party/md4c/src"
)

if(MSVC)
    target_compile_options(md4c_vendor PRIVATE /W3)
    target_compile_definitions(md4c_vendor PRIVATE _CRT_SECURE_NO_WARNINGS)
    set_property(
        TARGET md4c_vendor
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

add_library(
    markdownmay_prototype_core
    STATIC
    image_probe.cpp
    image_probe.hpp
    markdown_probe.cpp
    markdown_probe.hpp
    minimal_export.cpp
    minimal_export.hpp
)

target_include_directories(
    markdownmay_prototype_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(
    markdownmay_prototype_core
    PUBLIC
        md4c_vendor
    PRIVATE
        markdownmay_compiler_options
        ole32
        windowscodecs
)
