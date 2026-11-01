cmake_minimum_required(VERSION 3.16)
project(mylo_pixel_window_module LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(DEFINED ENV{MYLO_ROOT})
    set(MYLO_ROOT $ENV{MYLO_ROOT})
else()
    get_filename_component(MYLO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

if(APPLE)
    set(PW_SRC pixel_window_impl.mm)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    set(EXTRA_LIBS ${COCOA_FRAMEWORK} ${METAL_FRAMEWORK} ${QUARTZCORE_FRAMEWORK})
else()
    set(PW_SRC pixel_window_impl.c)
    if(WIN32)
        set(EXTRA_LIBS user32 gdi32)
    elseif(UNIX)
        find_package(X11 REQUIRED)
        set(EXTRA_LIBS X11::X11)
    endif()
endif()

add_library(pixel_window OBJECT ${PW_SRC})
target_include_directories(pixel_window PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${MYLO_ROOT}/src")
if(EXTRA_LIBS)
    target_link_libraries(pixel_window PUBLIC ${EXTRA_LIBS})
endif()

# Also build as a standalone shared module (.dll on Windows, .so on Unix)
add_library(pixel_window_shared SHARED ${PW_SRC})
set_target_properties(pixel_window_shared PROPERTIES
    OUTPUT_NAME "pixel_window"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(NOT WIN32)
    set_target_properties(pixel_window_shared PROPERTIES SUFFIX ".so")
endif()
target_include_directories(pixel_window_shared PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${MYLO_ROOT}/src")
if(EXTRA_LIBS)
    target_link_libraries(pixel_window_shared PUBLIC ${EXTRA_LIBS})
endif()
