cmake_minimum_required(VERSION 3.10)
project(SharpMark)

# Define ANSI escape sequences for colored output (works in most terminals)
if(NOT WIN32 OR (DEFINED ENV{TERM} AND NOT "$ENV{TERM}" STREQUAL ""))
    string(ASCII 27 Esc)
    set(ColorReset "${Esc}[m")
    set(ColorRed   "${Esc}[31m")
    set(ColorGreen "${Esc}[32m")
    set(ColorBold  "${Esc}[1m")
else()
    set(ColorReset "")
    set(ColorRed   "")
    set(ColorGreen "")
    set(ColorBold  "")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release build for performance (-O2)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Optimization flags for GCC/Clang
if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -march=native")
endif()

# Debug benchmark option
option(ENABLE_BENCHMARK "Enable debug benchmark output" OFF)
if(ENABLE_BENCHMARK)
    add_definitions(-DDEBUG_BENCHMARK)
endif()

# Try to find libraries (Linux/vcpkg/MSYS2 approach)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBRAW IMPORTED_TARGET libraw)
    pkg_check_modules(GTK3 IMPORTED_TARGET gtk+-3.0) 
endif()

if(NOT LIBRAW_FOUND)
    find_path(LIBRAW_INCLUDE_DIRS NAMES libraw/libraw.h)
    find_library(LIBRAW_LIBRARIES NAMES raw)
    if(LIBRAW_INCLUDE_DIRS AND LIBRAW_LIBRARIES)
        set(LIBRAW_FOUND TRUE)
    endif()
endif()

if(NOT LIBRAW_FOUND)
    message(FATAL_ERROR "${ColorRed}${ColorBold}LibRaw not found!${ColorReset}")
else()
    message(STATUS "${ColorGreen}LibRaw found.${ColorReset}")
endif()

if(NOT GTK3_FOUND)
    message(FATAL_ERROR "${ColorRed}${ColorBold}GTK3 not found!${ColorReset}")
else()
    message(STATUS "${ColorGreen}GTK3 found.${ColorReset}")
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/gui/window/app_style.cpp
    src/img_tools/laplacian.cpp
    src/img_tools/bmp.cpp
    src/gui/viewer/image_viewer.cpp
    src/gui/window/main_window_builder.cpp
    src/gui/utils/path_utils.cpp
    src/gui/actions/delete_blurry_action.cpp
    src/gui/actions/photo_trash.cpp
    src/tools/scan.cpp
    src/tools/XMP_tools.cpp
    src/gui/viewer/preview_loader.cpp
    src/gui/results/result_list_view.cpp
    src/gui/results/result_store.cpp
    src/gui/status/progress_summary.cpp
    src/gui/gui.cpp
)

if(WIN32)
    list(APPEND SOURCES assets/icons/app_icon.rc)
    add_executable(SharpMark WIN32 ${SOURCES})
else()
    add_executable(SharpMark ${SOURCES})
endif()

target_include_directories(SharpMark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(TARGET PkgConfig::LIBRAW)
    target_link_libraries(SharpMark PRIVATE PkgConfig::LIBRAW)
else()
    target_include_directories(SharpMark PRIVATE ${LIBRAW_INCLUDE_DIRS})
    target_link_libraries(SharpMark PRIVATE ${LIBRAW_LIBRARIES})
endif()

if(TARGET PkgConfig::GTK3)
    target_link_libraries(SharpMark PRIVATE PkgConfig::GTK3)
endif()

target_link_libraries(SharpMark PRIVATE Threads::Threads)
if(UNIX)
    target_link_libraries(SharpMark PRIVATE m)
endif()