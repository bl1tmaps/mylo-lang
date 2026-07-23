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

