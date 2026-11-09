cmake_minimum_required(VERSION 3.16)
project(mylo LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

# Optional extra text shown after the version number, e.g. -DMYLO_VERSION_SUFFIX=-beta.
set(MYLO_VERSION_SUFFIX "" CACHE STRING "Text added to the version number")
if(MYLO_VERSION_SUFFIX)
    add_compile_definitions(MYLO_VERSION_SUFFIX="${MYLO_VERSION_SUFFIX}")
endif()

# The newest tag and commit from `git describe`, refreshed on every build. Targets that
# show the version call mylo_use_git_version(<target>).
find_package(Git QUIET)
set(MYLO_GIT_VERSION_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(GIT_FOUND)
    add_custom_target(mylo_git_version
        COMMAND ${CMAKE_COMMAND}
            -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTPUT=${MYLO_GIT_VERSION_DIR}/mylo_git_version.h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GitVersion.cmake
        BYPRODUCTS ${MYLO_GIT_VERSION_DIR}/mylo_git_version.h
        COMMENT "Reading the git version"
        VERBATIM
    )
endif()
function(mylo_use_git_version target)
    if(TARGET mylo_git_version)
        add_dependencies(${target} mylo_git_version)
        target_include_directories(${target} PRIVATE ${MYLO_GIT_VERSION_DIR})
        target_compile_definitions(${target} PRIVATE MYLO_HAVE_GIT_VERSION)
    endif()
endfunction()

add_executable(mylo src/main.c src/compiler.c src/vm.h src/vm.c
        src/utils.h
        src/utils.c
        src/defines.h
        src/mylolib.h
        src/mylolib.c
        src/debug_adapter.c
        src/vm_options.c
)
add_executable(tests tests/tests.cpp src/compiler.c src/vm.h src/vm.c
        src/utils.c
        src/mylolib.h
        src/mylolib.c
        src/debug_adapter.c
        src/vm_options.c
)
add_executable(mylo-vsproject tools/mylo-vsproject.cpp)
add_executable(mylo-lsp lsp/mylo_lsp.c
        src/compiler.c src/vm.h src/vm.c
        src/utils.h src/utils.c
        src/defines.h
        src/mylolib.h src/mylolib.c
        src/debug_adapter.c
        src/vm_options.c
)
target_include_directories(mylo-lsp PRIVATE src)
mylo_use_git_version(mylo)
IF (WIN32)
  # Math not needed on Windows

    target_link_libraries(mylo ws2_32)
    target_link_libraries(tests ws2_32)
    target_link_libraries(mylo-lsp ws2_32)
ELSE()
  # Math needed on other plats
    target_link_libraries(mylo m)
    target_link_libraries(tests m)
    target_link_libraries(mylo-lsp m)
ENDIF()

# The tests throw C++ exceptions out of error callbacks, through the C compiler code.
# MSVC's default /EHsc assumes C functions never throw and drops the tests' try blocks.
if(MSVC)
    target_compile_options(tests PRIVATE /EHs /EHc-)
endif()

# Some tests write files, so they run in the build folder; this tells them where tests/ is.
target_compile_definitions(tests PRIVATE MYLO_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/")

enable_testing()
add_test(NAME mylo_tests COMMAND tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Drives `mylo --dap` through real debugging sessions.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME dap_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dap.py $<TARGET_FILE:mylo>)
endif()

set_target_properties(mylo PROPERTIES ENABLE_EXPORTS ON)
set_target_properties(tests PROPERTIES ENABLE_EXPORTS ON)
if(WIN32)
    set_target_properties(mylo PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    set_target_properties(tests PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
if(MINGW)
    target_link_options(mylo PRIVATE "-Wl,--export-all-symbols")
    target_link_options(tests PRIVATE "-Wl,--export-all-symbols")
endif()

option(MODULES "Build standalone Mylo modules" OFF)
if(MODULES)
  message(STATUS "Modules build flag enabled. Configuring plugins...")

  # The GTK module needs GTK3 installed (found through pkg-config), so it's skipped without it.
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(MYLO_GTK3 QUIET gtk+-3.0)
  endif()
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/modules/gtk" AND NOT MYLO_GTK3_FOUND)
    message(STATUS "Skipping module: GTK3 Bridge (GTK3 not found)")
  elseif(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/modules/gtk")
    message(STATUS "Configuring module: GTK3 Bridge")

    # Ensure the module compiles AFTER the main mylo executable is built,
    # since it relies on the compiler for binding generation!
    add_subdirectory(modules/gtk)
    add_dependencies(gtk_bridge mylo)
  endif()

  # RAYLIB Module
    
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/modules/raylib")      
    message(STATUS "Configuring module: Raylib")
    add_subdirectory(modules/raylib)
    add_dependencies(raylib_binding mylo)
  endif()

  # PIXEL_WINDOW Module
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/modules/pixel_window")
    message(STATUS "Configuring module: PixelWindow")
    add_subdirectory(modules/pixel_window)
    target_link_libraries(mylo pixel_window)
  endif()
else()
  message(STATUS "Skipping modules. Pass -DMODULES=ON to build them.")
endif()

# -----------------------------------------------------------------------------
# Mylo IDE (fetches raylib). Turn off with -DMYLO_BUILD_IDE=OFF for a core-only build.
# -----------------------------------------------------------------------------
option(MYLO_BUILD_IDE "Build Mylo IDE" ON)
if(MYLO_BUILD_IDE)
    add_subdirectory(ide)
endif()
