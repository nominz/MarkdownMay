add_executable(
    MarkdownMayPrototype
    WIN32
    main.cpp
    prototype.rc
    resource.h
)

target_link_libraries(
    MarkdownMayPrototype
    PRIVATE
        markdownmay_compiler_options
        comctl32
)

set_target_properties(
    MarkdownMayPrototype
    PROPERTIES
        OUTPUT_NAME "MarkdownMay-Prototype"
)

if(MSVC)
    target_link_options(
        MarkdownMayPrototype
        PRIVATE
            /MANIFEST:NO
            /DYNAMICBASE
            /NXCOMPAT
            /GUARD:CF
            $<$<CONFIG:Release>:/OPT:REF>
            $<$<CONFIG:Release>:/OPT:ICF>
    )
endif()
