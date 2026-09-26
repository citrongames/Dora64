cmake_minimum_required(VERSION 3.20)

project(Dora64 LANGUAGES C CXX)

# Development probes must not run during release gameplay, even if a device
# retains diagnostic system properties from a previous test session.
option(DORA64_ANDROID_DIAGNOSTICS "Enable Android frame/CPU diagnostic probes" OFF)
if(ANDROID AND DORA64_ANDROID_DIAGNOSTICS)
    add_compile_definitions(DORA64_ANDROID_DIAGNOSTICS)
endif()

# Game code is generated locally from the user's ROM, then patched for PC.
set(DORAEMON_GENERATED_DIR "${CMAKE_SOURCE_DIR}/build-tools/game" CACHE PATH
    "Patched output from tools/generate_game.py")
if(NOT DORAEMON_GENERATED_DIR)
    message(FATAL_ERROR "DORAEMON_GENERATED_DIR cannot be empty. Generate the game with tools/generate_game.py")
endif()
get_filename_component(DORAEMON_GENERATED_DIR "${DORAEMON_GENERATED_DIR}" ABSOLUTE)
if(NOT EXISTS "${DORAEMON_GENERATED_DIR}/generation-receipt.json")
    message(FATAL_ERROR
        "Generated game code is missing. From the repository root, run:\n"
        "  python3 tools/bootstrap_n64recomp.py\n"
        "  python3 tools/generate_game.py --rom <your.z64> --output-dir \"${DORAEMON_GENERATED_DIR}\"\n"
        "On Windows use py -3 instead of python3. See recomp/patches/README.md")
endif()
file(READ "${DORAEMON_GENERATED_DIR}/generation-receipt.json" DORAEMON_GENERATION_RECEIPT)
string(JSON DORAEMON_PC_PATCHED GET "${DORAEMON_GENERATION_RECEIPT}" pc_patches_applied)
if(NOT DORAEMON_PC_PATCHED)
    message(FATAL_ERROR "Use generate_game.py output, not the unpatched baseline")
endif()
string(JSON DORAEMON_APPLIED_PATCH_SHA GET "${DORAEMON_GENERATION_RECEIPT}" patch_sha256)
file(SHA256 "${CMAKE_SOURCE_DIR}/recomp/patches/game.patch" DORAEMON_EXPECTED_PATCH_SHA)
if(NOT DORAEMON_APPLIED_PATCH_SHA STREQUAL DORAEMON_EXPECTED_PATCH_SHA)
    message(FATAL_ERROR "Generated game patches are stale. Regenerate with tools/generate_game.py")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/recomp/patches/game.patch")
string(JSON DORAEMON_INPUT_COUNT LENGTH "${DORAEMON_GENERATION_RECEIPT}" input_sha256)
math(EXPR DORAEMON_INPUT_LAST "${DORAEMON_INPUT_COUNT} - 1")
foreach(input_index RANGE 0 ${DORAEMON_INPUT_LAST})
    string(JSON input_path MEMBER "${DORAEMON_GENERATION_RECEIPT}" input_sha256 ${input_index})
    string(JSON expected_hash GET "${DORAEMON_GENERATION_RECEIPT}" input_sha256 "${input_path}")
    file(SHA256 "${CMAKE_SOURCE_DIR}/${input_path}" actual_hash)
    if(NOT actual_hash STREQUAL expected_hash)
        message(FATAL_ERROR "Generated game is stale (${input_path}). Run tools/generate_game.py again")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/${input_path}")
endforeach()
set(DORAEMON_FUNCS_DIR "${DORAEMON_GENERATED_DIR}/RecompiledFuncs")
set(DORAEMON_RSP_SOURCE "${DORAEMON_GENERATED_DIR}/aspMain.cpp")
set(DORAEMON_GAME_HOOK_SOURCES src/doraemon_game_hooks.c)

if(ANDROID)
    set(SDL_SHARED ON CACHE BOOL "Build SDL2 shared library" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "Do not build SDL2 static library" FORCE)
    set(SDL_TEST OFF CACHE BOOL "Do not build SDL2 tests" FORCE)
    add_subdirectory(lib/SDL2)
endif()

if(TARGET SDL2::SDL2)
    # The Android project builds its pinned SDL2 checkout above.
elseif(WIN32)
    set(DORAEMON_SDL2_ROOT
        "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3")
    add_library(SDL2::SDL2 SHARED IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        IMPORTED_IMPLIB "${DORAEMON_SDL2_ROOT}/lib/x64/SDL2.lib"
        IMPORTED_LOCATION "${DORAEMON_SDL2_ROOT}/lib/x64/SDL2.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${DORAEMON_SDL2_ROOT}/include"
    )
else()
    find_package(SDL2 REQUIRED)
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(RT64_STATIC TRUE)
set(RT64_SDL_WINDOW_VULKAN TRUE)
set(DORAEMON_RUNTIME_ROOT "${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime" CACHE PATH
    "Path to N64ModernRuntime")

add_compile_definitions(HLSL_CPU)

add_subdirectory(lib/rt64)
add_subdirectory("${DORAEMON_RUNTIME_ROOT}" lib/N64ModernRuntime)

add_library(RecompiledFuncs STATIC)

file(GLOB FUNC_C_SOURCES
    "${DORAEMON_FUNCS_DIR}/*.c"
)

target_sources(RecompiledFuncs PRIVATE
    ${FUNC_C_SOURCES}
    ${DORAEMON_GAME_HOOK_SOURCES}
)

if(NOT MSVC)
    target_compile_options(RecompiledFuncs PRIVATE
        -fno-strict-aliasing
        -Wno-unused-variable
        -Wno-implicit-function-declaration
    )
endif()

target_include_directories(RecompiledFuncs PRIVATE
    ${DORAEMON_FUNCS_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${DORAEMON_RUNTIME_ROOT}/ultramodern/include
    ${DORAEMON_RUNTIME_ROOT}/librecomp/include
    ${DORAEMON_RUNTIME_ROOT}/N64Recomp/include
)

set(DORAEMON_APP_SOURCES
    src/main.cpp
    src/register_overlays.cpp
    ${DORAEMON_RSP_SOURCE}
    src/rt64_render_context.cpp
    src/doraemon_input.cpp
    src/doraemon_touch.cpp
    src/doraemon_audio.cpp
    src/doraemon_lighting.cpp
    src/doraemon_sky.cpp
    src/doraemon_camera.cpp
    src/doraemon_draw_distance.cpp
    src/doraemon_model_pool.cpp
    src/doraemon_draw_distance_diagnostics.cpp
    src/doraemon_cheats.cpp
    src/doraemon_localization.cpp
    src/system_overlay.cpp
)

if(ANDROID)
    add_library(Dora64 SHARED ${DORAEMON_APP_SOURCES})
else()
    add_executable(Dora64 ${DORAEMON_APP_SOURCES})
endif()

# Use the existing RT64 asset embedder; no image tools are needed to build.
if(NOT ANDROID)
set(DORAEMON_ICON_SOURCE "${CMAKE_SOURCE_DIR}/assets/icons/dora64_icon_linux_256.png")
set(DORAEMON_ICON_C "${CMAKE_CURRENT_BINARY_DIR}/generated/doraemon_icon.c")
set(DORAEMON_ICON_H "${CMAKE_CURRENT_BINARY_DIR}/generated/doraemon_icon.h")
add_custom_command(OUTPUT "${DORAEMON_ICON_C}" "${DORAEMON_ICON_H}"
    COMMAND file_to_c "${DORAEMON_ICON_SOURCE}" doraemon_icon_png
        "${DORAEMON_ICON_C}" "${DORAEMON_ICON_H}"
    DEPENDS "${DORAEMON_ICON_SOURCE}" file_to_c
    VERBATIM
)
target_sources(Dora64 PRIVATE "${DORAEMON_ICON_C}" "${DORAEMON_ICON_H}")
target_include_directories(Dora64 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
endif()

if(WIN32)
    enable_language(RC)
    configure_file(resources/windows/app.rc.in generated/doraemon.rc @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/doraemon.rc"
        PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/assets/icons/dora64_icon_win.ico")
    target_sources(Dora64 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/doraemon.rc")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Include desktop registration in portable builds without installing on build.
    add_custom_target(doraemon_desktop_files ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/resources/linux/dora64.desktop"
            "$<TARGET_FILE_DIR:Dora64>/dora64.desktop"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/resources/linux/install-desktop.py"
            "$<TARGET_FILE_DIR:Dora64>/install-desktop.py"
        VERBATIM
    )
    add_dependencies(Dora64 doraemon_desktop_files)
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64" AND NOT MSVC)
    target_compile_options(Dora64 PRIVATE
        -mssse3
        -msse4.1
    )
endif()

target_include_directories(Dora64 PRIVATE
    ${DORAEMON_FUNCS_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${DORAEMON_RUNTIME_ROOT}/ultramodern/include
    ${DORAEMON_RUNTIME_ROOT}/librecomp/include
    ${DORAEMON_RUNTIME_ROOT}/N64Recomp/include
    ${CMAKE_SOURCE_DIR}/lib/rt64/src
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/hlslpp/include
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/rhi
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/render
)

if(WIN32)
    target_compile_definitions(Dora64 PRIVATE
        NOMINMAX
    )
    target_link_directories(Dora64 PRIVATE
        "${DORAEMON_SDL2_ROOT}/lib/x64"
    )
    # The executable owns SDL's Windows entry point; do not rely on RT64
    # propagating SDL2main through its renderer dependencies.
    target_link_libraries(Dora64 PRIVATE SDL2main)
endif()

target_link_libraries(Dora64 PRIVATE
    RecompiledFuncs
    librecomp
    ultramodern
    rt64
    SDL2::SDL2
)

if(ANDROID)
    target_link_libraries(Dora64 PRIVATE android log)
endif()

# Refresh exports even when only assets changed and no relink is necessary.
if(NOT ANDROID)
add_custom_target(doraemon_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:Dora64>/assets"
    VERBATIM
)
add_dependencies(Dora64 doraemon_assets)
endif()

if(WIN32)
    add_custom_command(TARGET Dora64 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL2::SDL2>"
            "$<TARGET_FILE_DIR:Dora64>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxcompiler.dll"
            "$<TARGET_FILE_DIR:Dora64>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxil.dll"
            "$<TARGET_FILE_DIR:Dora64>"
    )
endif()

option(DORAEMON_BUILD_TESTS "Build headless regression tests" OFF)
if(DORAEMON_BUILD_TESTS)
    enable_testing()
    add_executable(touch_controls_test tests/touch_controls.cpp)
    target_include_directories(touch_controls_test PRIVATE src lib/rt64/src/contrib)
    add_test(NAME touch_controls COMMAND touch_controls_test)
    function(doraemon_extract_game_function function_name output_variable)
        foreach(source IN LISTS FUNC_C_SOURCES)
            file(READ "${source}" function_source)
            string(FIND "${function_source}" "RECOMP_FUNC void ${function_name}(" function_start)
            if(NOT function_start EQUAL -1)
                set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
                string(SUBSTRING "${function_source}" ${function_start} -1 function_body)
                string(FIND "${function_body}" "\nRECOMP_FUNC " next_function)
                if(NOT next_function EQUAL -1)
                    string(SUBSTRING "${function_body}" 0 ${next_function} function_body)
                endif()
                set(${output_variable} "${function_body}" PARENT_SCOPE)
                return()
            endif()
        endforeach()
        message(FATAL_ERROR "Generated function ${function_name} was not found")
    endfunction()
    find_package(Threads REQUIRED)
    add_executable(lighting_snapshots_test
        tests/lighting_snapshots.cpp
        src/doraemon_lighting.cpp
    )
    target_include_directories(lighting_snapshots_test PRIVATE src)
    target_link_libraries(lighting_snapshots_test PRIVATE Threads::Threads)
    add_test(NAME lighting_snapshots COMMAND lighting_snapshots_test)
    add_executable(decal_depth_test tests/decal_depth.cpp)
    target_include_directories(decal_depth_test PRIVATE lib/rt64/src)
    add_test(NAME decal_depth COMMAND decal_depth_test)
    add_executable(menu_background_test tests/menu_background.cpp)
    target_include_directories(menu_background_test PRIVATE lib/rt64/src)
    add_test(NAME menu_background COMMAND menu_background_test)
    doraemon_extract_game_function(func_8008010C SKY_FUNC_SOURCE)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/sky_reference.c" "#include \"recomp.h\"\n${SKY_FUNC_SOURCE}")
    add_executable(sky_panorama_test tests/sky_panorama.cpp src/doraemon_sky.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/generated/sky_reference.c")
    target_include_directories(sky_panorama_test PRIVATE src
        "${DORAEMON_RUNTIME_ROOT}/librecomp/include"
        "${DORAEMON_RUNTIME_ROOT}/ultramodern/include"
        "${DORAEMON_RUNTIME_ROOT}/N64Recomp/include")
    add_test(NAME sky_panorama COMMAND sky_panorama_test)
    add_executable(modern_camera_test tests/modern_camera.cpp src/doraemon_camera.cpp)
    target_include_directories(modern_camera_test PRIVATE src
        "${DORAEMON_RUNTIME_ROOT}/librecomp/include"
        "${DORAEMON_RUNTIME_ROOT}/ultramodern/include"
        "${DORAEMON_RUNTIME_ROOT}/N64Recomp/include")
    add_test(NAME modern_camera COMMAND modern_camera_test)
    add_executable(cheats_test tests/cheats.cpp src/doraemon_cheats.cpp)
    target_include_directories(cheats_test PRIVATE src
        "${DORAEMON_RUNTIME_ROOT}/librecomp/include"
        "${DORAEMON_RUNTIME_ROOT}/ultramodern/include"
        "${DORAEMON_RUNTIME_ROOT}/N64Recomp/include")
    add_test(NAME cheats COMMAND cheats_test)
    add_executable(draw_distance_test tests/draw_distance.cpp src/doraemon_draw_distance.cpp)
    target_include_directories(draw_distance_test PRIVATE src)
    add_test(NAME draw_distance COMMAND draw_distance_test)
    # Exercise the expanded pool with the game's actual model routines.
    doraemon_extract_game_function(func_8001F878 MODEL_FUNC_SOURCE)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/model_compactor.c" "#include \"recomp.h\"\n#include \"doraemon_model_pool.h\"\n${MODEL_FUNC_SOURCE}")
    doraemon_extract_game_function(func_8001F7FC POOL_FUNCTION)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/model_allocator.c" "#include \"recomp.h\"\n#include \"funcs.h\"\n#include \"doraemon_model_pool.h\"\n${POOL_FUNCTION}")
    doraemon_extract_game_function(func_8001CC70 POOL_FUNCTION)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/model_initializer.c" "#include \"recomp.h\"\n#include \"funcs.h\"\n#include \"doraemon_model_pool.h\"\n${POOL_FUNCTION}")
    doraemon_extract_game_function(func_8001E8A8 POOL_FUNCTION)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/model_position.c" "#include \"recomp.h\"\n#include \"funcs.h\"\n#include \"doraemon_model_pool.h\"\n${POOL_FUNCTION}")
    add_executable(model_pool_test tests/model_pool.cpp src/doraemon_model_pool.cpp src/doraemon_draw_distance.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/generated/model_compactor.c"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/model_allocator.c"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/model_initializer.c"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/model_position.c")
    target_include_directories(model_pool_test PRIVATE src "${DORAEMON_FUNCS_DIR}"
        "${DORAEMON_RUNTIME_ROOT}/librecomp/include"
        "${DORAEMON_RUNTIME_ROOT}/ultramodern/include"
        "${DORAEMON_RUNTIME_ROOT}/N64Recomp/include")
    add_test(NAME model_pool COMMAND model_pool_test)



endif()
