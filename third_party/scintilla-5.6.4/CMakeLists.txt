set(SCINTILLA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/scintilla")

file(
    GLOB
    SCINTILLA_CORE_SOURCES
    CONFIGURE_DEPENDS
    "${SCINTILLA_ROOT}/src/*.cxx"
)

add_library(
    scintilla_vendor
    STATIC
    ${SCINTILLA_CORE_SOURCES}
    "${SCINTILLA_ROOT}/win32/HanjaDic.cxx"
    "${SCINTILLA_ROOT}/win32/ListBox.cxx"
    "${SCINTILLA_ROOT}/win32/PlatWin.cxx"
    "${SCINTILLA_ROOT}/win32/ScintillaWin.cxx"
    "${SCINTILLA_ROOT}/win32/SurfaceD2D.cxx"
    "${SCINTILLA_ROOT}/win32/SurfaceGDI.cxx"
)

target_include_directories(
    scintilla_vendor
    PUBLIC
        "${SCINTILLA_ROOT}/include"
    PRIVATE
        "${SCINTILLA_ROOT}/src"
        "${SCINTILLA_ROOT}/win32"
)

target_compile_features(scintilla_vendor PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(
        scintilla_vendor
        PRIVATE
            /W0
            /utf-8
            /permissive-
            /EHsc
    )
    target_compile_definitions(
        scintilla_vendor
        PRIVATE
            UNICODE
            _UNICODE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
    )
endif()
