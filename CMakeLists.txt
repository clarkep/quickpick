# MSVC_DEBUG_INFORMATION_FORMAT Embedded will work with cmake >= 3.25. Versions below 3.25 will ignore
# it and use separate pdbs.
cmake_minimum_required(VERSION 3.16...3.25)

# include(FetchContent)
# Silence warning about setting timestamps when downloading dependencies
# cmake_policy(SET CMP0135 NEW)

project(quickpick)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
    set(_quickpick_deps_default vendored)
else()
    set(_quickpick_deps_default system)
endif()
set(QUICKPICK_DEPS_SOURCE ${_quickpick_deps_default} CACHE STRING
    "How to provide SDL3 and FreeType: system (installed), fetch (FetchContent), or vendored(Windows only: libs/")
set_property(CACHE QUICKPICK_DEPS_SOURCE PROPERTY STRINGS system fetch vendored)
option(QUICKPICK_DEPS_SHARED "Build SDL2 and freetype as shared libraries." ON)

if (UNIX)
    add_executable(quickpick quickpick.c autil.c math.c string.c containers.c window_sdl.c window_common.c draw_opengl.c draw_common.c platform_unix.c glad.c tlsf.c)
else()
    add_executable(quickpick quickpick.c autil.c math.c string.c containers.c window_sdl.c window_common.c draw_opengl.c draw_common.c platform_win.c glad.c tlsf.c)
endif()

if (QUICKPICK_DEPS_SOURCE STREQUAL vendored)
    add_library(quickpick_vendored_sdl3 STATIC IMPORTED)
    set_target_properties(quickpick_vendored_sdl3 PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/libs/SDL3-static.lib
        IMPORTED_LOCATION_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/libs/SDL3-staticd.lib
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include/extra)
    # System libs SDL3-static.lib needs at final link (same set as the
    # pragma comment(lib) list in window_sdl.c).
    set_property(TARGET quickpick_vendored_sdl3 PROPERTY INTERFACE_LINK_LIBRARIES
        user32 gdi32 winmm imm32 ole32 oleaut32 version uuid advapi32
        setupapi shell32 dinput8 shcore)

    add_library(quickpick_vendored_freetype STATIC IMPORTED)
    set_target_properties(quickpick_vendored_freetype PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/libs/freetype.lib
        IMPORTED_LOCATION_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/libs/freetyped.lib
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include/extra)

    set(_sdl_lib quickpick_vendored_sdl3)
    set(_freetype_lib quickpick_vendored_freetype)
elseif(QUICKPICK_DEPS_SOURCE STREQUAL fetch)
    include(FetchContent)
    set(SDL_SHARED OFF)
    set(SDL_STATIC ON)
    set(SDL_TEST_LIBRARY OFF)
    # Keep the FreeType build dependency-free.
    set(FT_DISABLE_ZLIB ON)
    set(FT_DISABLE_BZIP2 ON)
    set(FT_DISABLE_PNG ON)
    set(FT_DISABLE_HARFBUZZ ON)
    set(FT_DISABLE_BROTLI ON)
    set(SKIP_INSTALL_ALL ON)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG ${AUTIL_FETCH_SDL_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_Declare(freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG ${AUTIL_FETCH_FREETYPE_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3 freetype)
    set(_sdl_lib SDL3::SDL3-static)
    set(_freetype_lib freetype)
else()
    find_package(SDL3 CONFIG REQUIRED)
    find_package(Freetype REQUIRED)
    set(_sdl_lib SDL3::SDL3)
    set(_freetype_lib Freetype::Freetype)
endif()

target_link_libraries(quickpick PUBLIC ${_sdl_lib} PRIVATE ${_freetype_lib})
target_include_directories(quickpick PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(quickpick PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/extra)

if(UNIX AND NOT APPLE)  # or: if(NOT WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()

if(MSVC)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
        LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
    )
    # Supress warnings for integer-float conversions. Otherwise, the default
    # /W3 warnings are enabled.
    target_compile_options(${PROJECT_NAME} PRIVATE /wd4244)
    target_link_options(${PROJECT_NAME} PRIVATE /INCREMENTAL:NO)
endif()

# Checks if OSX and links appropriate frameworks (Only required on MacOS)
if (APPLE)
    target_link_libraries(${PROJECT_NAME} "-framework IOKit")
    target_link_libraries(${PROJECT_NAME} "-framework Cocoa")
    target_link_libraries(${PROJECT_NAME} "-framework OpenGL")
endif()
