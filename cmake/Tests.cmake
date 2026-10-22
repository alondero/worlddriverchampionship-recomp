if(BUILD_TESTING)
    add_executable(wdc_hud_stream_tests tests/test_hud_stream.cpp)
    target_include_directories(wdc_hud_stream_tests PRIVATE src)
    add_test(NAME wdc_hud_stream COMMAND wdc_hud_stream_tests)
    add_executable(wdc_rdp_triangle_clip_tests tests/test_rdp_triangle_clip.cpp)
    target_include_directories(wdc_rdp_triangle_clip_tests PRIVATE lib/rt64/src)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_definitions(wdc_rdp_triangle_clip_tests PRIVATE _GLIBCXX_ASSERTIONS)
    endif()
    add_test(NAME wdc_rdp_triangle_clip COMMAND wdc_rdp_triangle_clip_tests)
    add_executable(wdc_rdp_triangle_edge_tests tests/test_rdp_triangle_edges.cpp)
    target_include_directories(wdc_rdp_triangle_edge_tests PRIVATE lib/rt64/src)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_definitions(wdc_rdp_triangle_edge_tests PRIVATE _GLIBCXX_ASSERTIONS)
    endif()
    add_test(NAME wdc_rdp_triangle_edges COMMAND wdc_rdp_triangle_edge_tests)
    add_executable(wdc_rsp_packed_load_tests tests/test_rsp_packed_loads.cpp)
    target_include_directories(wdc_rsp_packed_load_tests PRIVATE
        "${N64MR}/N64Recomp/include" "${N64MR}/librecomp/include" "${N64MR}/ultramodern/include")
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
        if(NOT WDC_TESTS_ONLY AND TARGET WdcRecompiledFuncs AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/World Driver Championship (USA).z64")
            add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/camera_test_funcs.c"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/extract_camera_test.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs" "${CMAKE_CURRENT_BINARY_DIR}/camera_test_funcs.c"
                DEPENDS tests/extract_camera_test.py ${WDC_FUNC_C_SOURCES}
                VERBATIM)
            add_executable(wdc_camera_rom_tests tests/test_camera_rom.cpp src/game_camera.cpp src/game_hud.cpp src/game_visibility.cpp
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
        add_test(NAME wdc_rdp_seams
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rdp_seams.py")
        add_test(NAME wdc_bootstrap_structure
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bootstrap.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
endif()
