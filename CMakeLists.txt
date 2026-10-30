cmake_minimum_required(VERSION 3.5)
project(mylo LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

add_executable(mylo src/main.c src/compiler.c src/vm.h src/vm.c
        src/utils.h
        src/utils.c
        src/defines.h
        src/mylolib.h
        src/mylolib.c
        src/debug_adapter.c
)
add_executable(tests tests/tests.cpp src/compiler.c src/vm.h src/vm.c
        src/utils.c
        src/mylolib.h
        src/mylolib.c
        src/debug_adapter.c
)
add_executable(mylo-vsproject tools/mylo-vsproject.cpp)
IF (WIN32)
  # Math not needed on Windows

    target_link_libraries(mylo ws2_32)
    target_link_libraries(tests ws2_32)
ELSE()
  # Math needed on other plats
    target_link_libraries(mylo m)
    target_link_libraries(tests m)
ENDIF()

set_target_properties(mylo PROPERTIES ENABLE_EXPORTS ON)

option(MODULES "Build standalone Mylo modules" OFF)
if(MODULES)
  message(STATUS "Modules build flag enabled. Configuring plugins...")

  # Automatically build the GTK module if the directory exists
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/modules/gtk")
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
