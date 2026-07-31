add_library(markdownmay_compiler_options INTERFACE)

target_compile_features(markdownmay_compiler_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(
        markdownmay_compiler_options
        INTERFACE
            /W4
            /WX
            /permissive-
            /utf-8
            /EHsc
            /Zc:__cplusplus
            /Zc:preprocessor
    )

    target_compile_definitions(
        markdownmay_compiler_options
        INTERFACE
            UNICODE
            _UNICODE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
    )

else()
    message(FATAL_ERROR "The stage-3 prototype currently requires MSVC on Windows.")
endif()
