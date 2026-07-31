add_executable(
    MarkdownMayScintillaProbe
    WIN32
    main.cpp
)

target_link_libraries(
    MarkdownMayScintillaProbe
    PRIVATE
        markdownmay_compiler_options
        scintilla_vendor
        advapi32
        d2d1
        dwrite
        gdi32
        imm32
        ole32
        oleaut32
        user32
        windowscodecs
)

set_target_properties(
    MarkdownMayScintillaProbe
    PROPERTIES
        OUTPUT_NAME "MarkdownMay-Scintilla-Probe"
)

if(MSVC)
    target_link_options(
        MarkdownMayScintillaProbe
        PRIVATE
            /DYNAMICBASE
            /NXCOMPAT
            /GUARD:CF
            $<$<CONFIG:Release>:/OPT:REF>
            $<$<CONFIG:Release>:/OPT:ICF>
    )
endif()
