cmake_minimum_required(VERSION 3.14) # FetchContent requires 3.11+
project(mylo_raylib_module LANGUAGES C)

# Match the core Mylo C standards
set(CMAKE_C_STANDARD 11)

# 1. Resolve the Mylo Root Directory
if(DEFINED ENV{MYLO_ROOT})
    set(MYLO_ROOT $ENV{MYLO_ROOT})
else()
    # Fallback: Assume we are in mylo-lang/modules/raylib/
    get_filename_component(MYLO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

# 2. Smartly locate the 'mylo' executable
if(TARGET mylo)
    set(MYLO_EXE "$<TARGET_FILE:mylo>")
else()
    if(EXISTS "${MYLO_ROOT}/build/mylo")
        set(MYLO_EXE "${MYLO_ROOT}/build/mylo")
    elseif(EXISTS "${MYLO_ROOT}/mylo")
        set(MYLO_EXE "${MYLO_ROOT}/mylo")
    else()
        message(FATAL_ERROR "Could not find 'mylo' executable! Build the core project first.")
    endif()
endif()

# 3. Raylib Configuration & Fetching
option(MAKE_MYLO_STATIC "Build dependencies statically for portability" ON)
if(MAKE_MYLO_STATIC)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    # CRITICAL: Static libs must be compiled with PIC to link into our Shared Object (.so)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
else()
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
endif()
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# If you manually put the folder back, it will use it. Otherwise, it downloads it!
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/raylib-5.5/CMakeLists.txt")
    add_subdirectory(raylib-5.5)
else()
    message(STATUS "Raylib directory not found. Fetching Raylib 5.5 automatically...")
    include(FetchContent)
    FetchContent_Declare(
        raylib
        URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
    )
    FetchContent_MakeAvailable(raylib)
endif()

# 4. Generate Mylo Bindings for Raylib
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/raylib_binding.mylo" "${CMAKE_CURRENT_BINARY_DIR}/raylib_binding.mylo" COPYONLY)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/raylib_binding.mylo_bind.c"
    COMMAND ${MYLO_EXE} --bind "raylib_binding.mylo"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/raylib_binding.mylo"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Generating Mylo C bindings for Raylib..."
)

# 5. Build the Shared Library (Module/Plugin)
add_library(raylib_binding MODULE "${CMAKE_CURRENT_BINARY_DIR}/raylib_binding.mylo_bind.c")
set_target_properties(raylib_binding PROPERTIES PREFIX "") # Compiles to 'raylib_binding.so'

target_include_directories(raylib_binding PRIVATE "${MYLO_ROOT}/src")
target_link_libraries(raylib_binding PRIVATE raylib)

# 6. Build the Standalone 'my_game' Executable
# Copy the game source and the cat image to the build folder so it runs properly
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/example.mylo" "${CMAKE_CURRENT_BINARY_DIR}/example.mylo" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cat.png" "${CMAKE_CURRENT_BINARY_DIR}/cat.png" COPYONLY)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/example_out.c"
    COMMAND ${MYLO_EXE} --build "example.mylo"
    COMMAND ${CMAKE_COMMAND} -E rename "out.c" "example_out.c"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/example.mylo"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Compiling example.mylo into a standalone C executable..."
)

add_executable(my_game 
    "${CMAKE_CURRENT_BINARY_DIR}/example_out.c"
    "${MYLO_ROOT}/src/vm.c"
    "${MYLO_ROOT}/src/mylolib.c"
    "${MYLO_ROOT}/src/utils.c"
    "${MYLO_ROOT}/src/debug_adapter.c"
    "${MYLO_ROOT}/src/compiler.c"
    "${MYLO_ROOT}/src/vm_options.c"
)

target_include_directories(my_game PRIVATE "${MYLO_ROOT}/src")
target_link_libraries(my_game PRIVATE raylib)

if(NOT WIN32)
    target_link_libraries(my_game PRIVATE m)
endif()
