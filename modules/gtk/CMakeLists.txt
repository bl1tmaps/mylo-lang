cmake_minimum_required(VERSION 3.10)
project(mylo_gtk_module LANGUAGES C)

# Match the core Mylo C standards
set(CMAKE_C_STANDARD 11)

# 1. Resolve the Mylo Root Directory
if(DEFINED ENV{MYLO_ROOT})
    set(MYLO_ROOT $ENV{MYLO_ROOT})
else()
    # Fallback: Assume we are in mylo-lang/modules/gtk/
    get_filename_component(MYLO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

# 2. Smartly locate the 'mylo' executable
if(TARGET mylo)
    # Unified build: CMake knows exactly where the target is being built, 
    # regardless of whether it is in the root or a separate build/ folder.
    set(MYLO_EXE "$<TARGET_FILE:mylo>")
    set(MYLO_TARGET mylo)
else()
    set(MYLO_TARGET "")
    # Standalone build: Look in the build folder first, then the root
    if(EXISTS "${MYLO_ROOT}/build/mylo")
        set(MYLO_EXE "${MYLO_ROOT}/build/mylo")
    elseif(EXISTS "${MYLO_ROOT}/build/Debug/mylo.exe")
        set(MYLO_EXE "${MYLO_ROOT}/build/Debug/mylo.exe")
    elseif(EXISTS "${MYLO_ROOT}/build/Release/mylo.exe")
        set(MYLO_EXE "${MYLO_ROOT}/build/Release/mylo.exe")
    elseif(EXISTS "${MYLO_ROOT}/mylo.exe")
        set(MYLO_EXE "${MYLO_ROOT}/mylo.exe")
    elseif(EXISTS "${MYLO_ROOT}/mylo")
        set(MYLO_EXE "${MYLO_ROOT}/mylo")
    else()
        message(FATAL_ERROR "Could not find 'mylo' executable! Build the core project first.")
    endif()
endif()

# 3. Find GTK3 using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

# 4. Copy source files to the build directory 
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/gtk_bridge.mylo" "${CMAKE_CURRENT_BINARY_DIR}/gtk_bridge.mylo" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/gtk_state.h" "${CMAKE_CURRENT_BINARY_DIR}/gtk_state.h" COPYONLY)

# 5. Generate the binding C code using the Mylo compiler
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gtk_bridge.mylo_bind.c"
    COMMAND ${MYLO_EXE} --bind "gtk_bridge.mylo"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/gtk_bridge.mylo" "${CMAKE_CURRENT_BINARY_DIR}/gtk_state.h" ${MYLO_TARGET}
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Generating Mylo C bindings for GTK..."
)

# 6. Build the Shared Library (Module/Plugin)
add_library(gtk_bridge MODULE "${CMAKE_CURRENT_BINARY_DIR}/gtk_bridge.mylo_bind.c")
if(TARGET mylo)
    add_dependencies(gtk_bridge mylo)
endif()

# Remove the 'lib' prefix so it compiles as 'gtk_bridge.so'
set_target_properties(gtk_bridge PROPERTIES PREFIX "")

# 7. Include headers and link GTK3
target_include_directories(gtk_bridge PRIVATE 
    "${MYLO_ROOT}/src" 
    ${GTK3_INCLUDE_DIRS}
)

target_link_directories(gtk_bridge PRIVATE ${GTK3_LIBRARY_DIRS})
target_link_libraries(gtk_bridge PRIVATE ${GTK3_LIBRARIES})
