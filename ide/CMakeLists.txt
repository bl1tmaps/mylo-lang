# Mylo IDE: the editor, debugger, memory viewer and VM settings panel.
#
# raylib 5.5 is fetched from GitHub unless MYLO_RAYLIB_DIR points at a local copy.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MYLO_RAYLIB_DIR "" CACHE PATH "A local raylib 5.5 source tree to use instead of downloading one")
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
if(TARGET raylib)
    # Already added, by the raylib module when building with -DMODULES=ON.
elseif(MYLO_RAYLIB_DIR)
    add_subdirectory("${MYLO_RAYLIB_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/raylib" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(raylib)
endif()

find_package(Threads REQUIRED)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)

# The IDE compiles scripts in-process for completions and error markers.
set(MYLO_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(MYLO_CORE_SOURCES
    ${MYLO_CORE_DIR}/compiler.c
    ${MYLO_CORE_DIR}/vm.c
    ${MYLO_CORE_DIR}/utils.c
    ${MYLO_CORE_DIR}/mylolib.c
    ${MYLO_CORE_DIR}/debug_adapter.c
    ${MYLO_CORE_DIR}/vm_options.c
)

add_executable(mylo-ide
    src/main.cpp
    src/ide_app.cpp
    src/debug_session.cpp
    src/process.cpp
    src/json.cpp
    src/TextEditor.cpp
    src/mylo_lsp.cpp
    src/imgui_impl_raylib.cpp
    ${IMGUI_SOURCES}
    ${MYLO_CORE_SOURCES}
)
target_include_directories(mylo-ide PRIVATE src ${IMGUI_DIR} ${MYLO_CORE_DIR})
target_link_libraries(mylo-ide PRIVATE raylib Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(mylo-ide PRIVATE ws2_32)
    # A windowed program, so starting it doesn't open a console as well.
    set_target_properties(mylo-ide PROPERTIES WIN32_EXECUTABLE ON)
    if(MSVC)
        target_link_options(mylo-ide PRIVATE /ENTRY:mainCRTStartup)
    endif()
else()
    target_link_libraries(mylo-ide PRIVATE m)
endif()
if(APPLE)
    target_link_libraries(mylo-ide PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()

# The fonts go beside the executable, where the IDE looks first.
add_custom_command(TARGET mylo-ide POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts" "$<TARGET_FILE_DIR:mylo-ide>/assets/fonts")

# The IDE runs scripts with mylo, so build it alongside.
add_dependencies(mylo-ide mylo)

# -----------------------------------------------------------------------------
# text_editor_fuzz: random editing sessions against the editor, checked step by step.
# Run it after changing TextEditor. MYLO_FUZZ_SANITIZE adds AddressSanitizer.
# -----------------------------------------------------------------------------
option(MYLO_FUZZ_SANITIZE "Build text_editor_fuzz with AddressSanitizer" OFF)
set(MYLO_FUZZ_SEEDS 300 CACHE STRING "Editing sessions the text_editor_fuzz test runs")

add_executable(text_editor_fuzz tests/text_editor_fuzz.cpp src/TextEditor.cpp ${IMGUI_SOURCES})
target_include_directories(text_editor_fuzz PRIVATE src ${IMGUI_DIR} ${MYLO_CORE_DIR})
if(NOT WIN32)
    target_link_libraries(text_editor_fuzz PRIVATE m)
endif()
if(MYLO_FUZZ_SANITIZE)
    if(MSVC)
        target_compile_options(text_editor_fuzz PRIVATE /fsanitize=address /Zi)
        target_link_options(text_editor_fuzz PRIVATE /INCREMENTAL:NO)
        # The ASan runtime is only on PATH in a developer prompt, so copy it beside the test.
        get_filename_component(MSVC_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        file(GLOB MSVC_ASAN_DLLS "${MSVC_BIN_DIR}/clang_rt.asan*.dll")
        foreach(dll ${MSVC_ASAN_DLLS})
            add_custom_command(TARGET text_editor_fuzz POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:text_editor_fuzz>")
        endforeach()
    else()
        set(FUZZ_SANITIZE_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
        target_compile_options(text_editor_fuzz PRIVATE ${FUZZ_SANITIZE_FLAGS})
        target_link_options(text_editor_fuzz PRIVATE ${FUZZ_SANITIZE_FLAGS})
    endif()
endif()
add_test(NAME text_editor_fuzz COMMAND text_editor_fuzz 1 ${MYLO_FUZZ_SEEDS} 400)
