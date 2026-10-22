cmake_minimum_required(VERSION 3.20)

# World Driver Championship: Recompiled. This top-level file owns the seams
# between generated ROM code, the shared runtime/renderer, and the port.
# BUILDING.md documents the two-pass configure/recompile/build sequence.

project(worlddriverchampionship_modern VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
find_package(Threads REQUIRED)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(WDC_ENABLE_FRONTEND "Build the RecompFrontend settings overlay" ON)
option(WDC_ENABLE_ANDROID "Build the Android-specific native bridge" OFF)
option(WDC_HEADLESS "Build the bounded CPU startup diagnostic without RT64" OFF)

set(N64MR "${CMAKE_CURRENT_SOURCE_DIR}/lib/N64ModernRuntime")
if(ANDROID OR WDC_ENABLE_ANDROID)
    include(cmake/Android.cmake)
endif()

if(NOT EXISTS "${N64MR}/CMakeLists.txt")
    message(FATAL_ERROR "lib/N64ModernRuntime is missing. Run: git submodule update --init --recursive")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/CMakeLists.txt")
    message(FATAL_ERROR "lib/rt64 is missing. Run: git submodule update --init --recursive")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/RecompFrontend/CMakeLists.txt")
    message(FATAL_ERROR "lib/RecompFrontend is missing. Run: git submodule update --init --recursive")
endif()

# The shared runtime contains N64Recomp and RSPRecomp host tools. It is added
# before the generated image so the first configure can build those tools even
# though RecompiledFuncs/ does not exist yet.
add_subdirectory("${N64MR}")

if(NOT WDC_HEADLESS)
set(RT64_STATIC TRUE)
if(WIN32)
    set(SDL2_WIN32_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3")
    set(SDL2_INCLUDE_DIRS "${SDL2_WIN32_DEPS}/include")
    set(SDL2_LIBRARIES SDL2 SDL2main)
    link_directories("${SDL2_WIN32_DEPS}/lib/x64")
    include(FetchContent)
    FetchContent_Declare(DirectX-Headers
        GIT_REPOSITORY https://github.com/microsoft/DirectX-Headers
        GIT_TAG v1.614.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(DirectX-Headers)
    include_directories(BEFORE
        "${directx-headers_SOURCE_DIR}/include"
        "${directx-headers_SOURCE_DIR}/include/directx"
        "${directx-headers_SOURCE_DIR}/include/dxguids")
else()
    set(RT64_SDL_WINDOW_VULKAN TRUE)
    set(NFD_PORTAL ON CACHE BOOL "" FORCE)
endif()
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "Do not register vendored compression benchmarks" FORCE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "Only the compression library is needed" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64" "${CMAKE_BINARY_DIR}/rt64")
target_include_directories(rt64 PRIVATE "${CMAKE_BINARY_DIR}/rt64/src")

if(WIN32)
    target_link_libraries(plume PRIVATE DirectX-Guids)
    target_link_libraries(rt64 DirectX-Guids dwmapi d3dcompiler)
endif()
if(NOT WIN32 AND NOT ANDROID)
    find_package(SDL2 REQUIRED)
endif()
endif()

# Generated CPU code is a separate library. CONFIGURE_DEPENDS plus the second
# configure in build.ps1/build.sh makes newly generated functions visible.
file(GLOB WDC_FUNC_C_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs/*.c")
file(GLOB WDC_FUNC_CXX_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs/*.cpp")
if(WDC_FUNC_C_SOURCES OR WDC_FUNC_CXX_SOURCES)
    add_library(WdcRecompiledFuncs STATIC ${WDC_FUNC_C_SOURCES} ${WDC_FUNC_CXX_SOURCES})
    target_compile_options(WdcRecompiledFuncs PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/src/recomp_compat.h"
        -fno-strict-aliasing -ggdb -Wno-unused-variable
        $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration>)
    target_include_directories(WdcRecompiledFuncs PRIVATE
        "${N64MR}/ultramodern/include"
        "${N64MR}/librecomp/include"
        "${N64MR}/N64Recomp/include")
endif()

set(WDC_SOURCES src/main.cpp src/register_overlays.cpp src/host.cpp src/guest_math.cpp src/game_camera.cpp src/game_visibility.cpp src/boss_rsp.cpp
    src/audio_ai.cpp src/audio_dma.cpp src/controller_pak.cpp)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/aspMain.cpp")
    list(APPEND WDC_SOURCES src/aspMain.cpp)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|amd64|x86_64")
        set_source_files_properties(src/aspMain.cpp PROPERTIES COMPILE_OPTIONS "-msse4.1")
    endif()
endif()

if(ANDROID)
    add_library(worlddriverchampionship_modern SHARED ${WDC_SOURCES})
    set_target_properties(worlddriverchampionship_modern PROPERTIES OUTPUT_NAME main)
else()
    add_executable(worlddriverchampionship_modern ${WDC_SOURCES})
endif()

target_compile_options(worlddriverchampionship_modern PRIVATE -fno-strict-aliasing -ggdb)
target_include_directories(worlddriverchampionship_modern PRIVATE
    "${N64MR}/ultramodern/include"
    "${N64MR}/librecomp/include"
    "${N64MR}/N64Recomp/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs")
if(TARGET WdcRecompiledFuncs)
    target_link_libraries(worlddriverchampionship_modern PRIVATE WdcRecompiledFuncs)
endif()
target_link_libraries(worlddriverchampionship_modern PRIVATE
    librecomp ultramodern Threads::Threads)
if(NOT WDC_HEADLESS)
    target_link_libraries(worlddriverchampionship_modern PRIVATE rt64)
    target_sources(worlddriverchampionship_modern PRIVATE src/renderer.cpp src/audio.cpp
        src/graphics_config.cpp src/graphics_window.cpp)
    if(MINGW)
        target_sources(worlddriverchampionship_modern PRIVATE src/mingw_guids.cpp)
    endif()
    target_include_directories(worlddriverchampionship_modern PRIVATE lib/rt64/src
        lib/rt64/src/contrib/hlslpp/include lib/rt64/src/contrib ${SDL2_INCLUDE_DIRS})
    target_compile_definitions(worlddriverchampionship_modern PRIVATE WDC_PLAYER=1 SDL_MAIN_HANDLED NOMINMAX)
    target_link_libraries(worlddriverchampionship_modern PRIVATE ${SDL2_LIBRARIES})
    if(WIN32)
        foreach(dll dxcompiler dxil)
            add_custom_command(TARGET worlddriverchampionship_modern POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/${dll}.dll"
                    "$<TARGET_FILE_DIR:worlddriverchampionship_modern>")
        endforeach()
        add_custom_command(TARGET worlddriverchampionship_modern POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SDL2_WIN32_DEPS}/lib/x64/SDL2.dll"
                "$<TARGET_FILE_DIR:worlddriverchampionship_modern>")
        if(MINGW)
            get_filename_component(WDC_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
            foreach(dll libgcc_s_seh-1 libstdc++-6 libwinpthread-1)
                add_custom_command(TARGET worlddriverchampionship_modern POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${WDC_COMPILER_DIR}/${dll}.dll"
                        "$<TARGET_FILE_DIR:worlddriverchampionship_modern>")
            endforeach()
        endif()
    endif()
endif()

if(WDC_ENABLE_FRONTEND AND NOT WDC_HEADLESS)
    include(cmake/Frontend.cmake)
endif()

if(BUILD_TESTING)
    add_executable(wdc_rdp_triangle_clip_tests tests/test_rdp_triangle_clip.cpp)
    target_include_directories(wdc_rdp_triangle_clip_tests PRIVATE lib/rt64/src)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_definitions(wdc_rdp_triangle_clip_tests PRIVATE _GLIBCXX_ASSERTIONS)
    endif()
    add_test(NAME wdc_rdp_triangle_clip COMMAND wdc_rdp_triangle_clip_tests)
    add_executable(wdc_rsp_packed_load_tests tests/test_rsp_packed_loads.cpp)
    target_include_directories(wdc_rsp_packed_load_tests PRIVATE "${N64MR}/N64Recomp/include")
    target_link_libraries(wdc_rsp_packed_load_tests PRIVATE librecomp)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|amd64|x86_64")
        target_compile_options(wdc_rsp_packed_load_tests PRIVATE -msse4.1)
    endif()
    add_test(NAME wdc_rsp_packed_loads COMMAND wdc_rsp_packed_load_tests)
    add_executable(wdc_audio_dma_tests tests/test_audio_dma.cpp src/audio_dma.cpp)
    target_include_directories(wdc_audio_dma_tests PRIVATE src)
    add_test(NAME wdc_audio_dma COMMAND wdc_audio_dma_tests)
    add_executable(wdc_audio_tests tests/test_audio.cpp src/audio.cpp)
    target_include_directories(wdc_audio_tests PRIVATE tests/audio_sdl src)
    target_link_libraries(wdc_audio_tests PRIVATE Threads::Threads)
    add_test(NAME wdc_audio_output COMMAND wdc_audio_tests)
    add_executable(wdc_controller_pak_tests tests/test_controller_pak.cpp src/controller_pak.cpp)
    target_include_directories(wdc_controller_pak_tests PRIVATE src
        "${N64MR}/N64Recomp/include" "${N64MR}/ultramodern/include")
    target_link_libraries(wdc_controller_pak_tests PRIVATE Threads::Threads)
    add_test(NAME wdc_controller_pak COMMAND wdc_controller_pak_tests)
    add_executable(wdc_graphics_config_tests tests/test_graphics_config.cpp src/graphics_config.cpp src/game_visibility.cpp)
    target_include_directories(wdc_graphics_config_tests PRIVATE src "${N64MR}/thirdparty" "${N64MR}/N64Recomp/include")
    target_compile_definitions(wdc_graphics_config_tests PRIVATE NOMINMAX)
    target_link_libraries(wdc_graphics_config_tests PRIVATE Threads::Threads)
    add_test(NAME wdc_graphics_config COMMAND wdc_graphics_config_tests)
    add_executable(wdc_guest_math_tests tests/test_guest_math.cpp src/guest_math.cpp)
    target_include_directories(wdc_guest_math_tests PRIVATE "${N64MR}/N64Recomp/include")
    add_test(NAME wdc_guest_math COMMAND wdc_guest_math_tests)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        if(TARGET WdcRecompiledFuncs AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/World Driver Championship (USA).z64")
            add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/camera_test_funcs.c"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/extract_camera_test.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs" "${CMAKE_CURRENT_BINARY_DIR}/camera_test_funcs.c"
                DEPENDS tests/extract_camera_test.py ${WDC_FUNC_C_SOURCES}
                VERBATIM)
            add_executable(wdc_camera_rom_tests tests/test_camera_rom.cpp src/game_camera.cpp src/game_visibility.cpp
                "${CMAKE_CURRENT_BINARY_DIR}/camera_test_funcs.c")
            target_include_directories(wdc_camera_rom_tests PRIVATE src "${N64MR}/N64Recomp/include")
            target_compile_options(wdc_camera_rom_tests PRIVATE -fno-strict-aliasing)
            add_test(NAME wdc_camera_rom COMMAND wdc_camera_rom_tests
                "${CMAKE_CURRENT_SOURCE_DIR}/World Driver Championship (USA).z64")
            set_tests_properties(wdc_camera_rom PROPERTIES TIMEOUT 30)
            add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/visibility_test_funcs.c"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/extract_visibility_test.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs" "${CMAKE_CURRENT_BINARY_DIR}/visibility_test_funcs.c"
                DEPENDS tests/extract_visibility_test.py ${WDC_FUNC_C_SOURCES}
                VERBATIM)
            add_executable(wdc_visibility_rom_tests tests/test_visibility_rom.cpp src/game_visibility.cpp
                "${CMAKE_CURRENT_BINARY_DIR}/visibility_test_funcs.c")
            target_include_directories(wdc_visibility_rom_tests PRIVATE src "${N64MR}/N64Recomp/include")
            target_compile_options(wdc_visibility_rom_tests PRIVATE -fno-strict-aliasing)
            add_test(NAME wdc_visibility_rom COMMAND wdc_visibility_rom_tests
                "${CMAKE_CURRENT_SOURCE_DIR}/World Driver Championship (USA).z64")
            set_tests_properties(wdc_visibility_rom PROPERTIES TIMEOUT 30)
            add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/pfs_test_funcs.c"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/extract_pfs_test.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs" "${CMAKE_CURRENT_BINARY_DIR}/pfs_test_funcs.c"
                DEPENDS tests/extract_pfs_test.py ${WDC_FUNC_C_SOURCES}
                VERBATIM)
            add_executable(wdc_pfs_rom_tests tests/test_pfs_rom.cpp src/controller_pak.cpp
                "${CMAKE_CURRENT_BINARY_DIR}/pfs_test_funcs.c")
            target_include_directories(wdc_pfs_rom_tests PRIVATE src "${N64MR}/N64Recomp/include"
                "${N64MR}/ultramodern/include")
            target_compile_options(wdc_pfs_rom_tests PRIVATE -fno-strict-aliasing)
            target_link_libraries(wdc_pfs_rom_tests PRIVATE Threads::Threads)
            add_test(NAME wdc_pfs_rom COMMAND wdc_pfs_rom_tests
                "${CMAKE_CURRENT_SOURCE_DIR}/World Driver Championship (USA).z64")
            set_tests_properties(wdc_pfs_rom PROPERTIES TIMEOUT 30)
        endif()
        add_test(NAME wdc_rom_transforms
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rom_transforms.py")
        add_test(NAME wdc_bootstrap_structure
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bootstrap.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
endif()
