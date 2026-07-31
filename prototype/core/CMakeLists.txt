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
