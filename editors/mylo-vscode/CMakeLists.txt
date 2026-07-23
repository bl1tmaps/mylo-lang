cmake_minimum_required(VERSION 3.5)
project(mylo-vscode)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

# Set the output directory to bin/ in the current directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# Sources
set(CORE_SRC
    ../../src/compiler.c
    ../../src/vm.c
    ../../src/utils.c
    ../../src/debug_adapter.c
    ../../src/mylolib.c
)

# Build mylo
add_executable(mylo ../../src/main.c ${CORE_SRC})
target_include_directories(mylo PRIVATE ../../src)

# Build mylo-lsp
add_executable(mylo-lsp ../../lsp/mylo_lsp.c ${CORE_SRC})
target_include_directories(mylo-lsp PRIVATE ../../src)

if (WIN32)
    target_link_libraries(mylo ws2_32)
    target_link_libraries(mylo-lsp ws2_32)
else()
    target_link_libraries(mylo m)
    target_link_libraries(mylo-lsp m)
endif()

# Custom command to package the VSCode extension
add_custom_target(package-vsix
    COMMAND npm install
    COMMAND npx vsce package
    DEPENDS mylo mylo-lsp
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Packaging VSCode Extension..."
)
