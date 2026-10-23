# The settings overlay shares RT64's presentation framebuffer. The port keeps
# graphics persistence/window changes on SDL's main thread.
set(WDC_DXC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/dxc")
if(ANDROID)
    set(DXC "${WDC_HOST_DXC}")
elseif(WIN32)
    set(DXC "${WDC_DXC_ROOT}/bin/x64/dxc.exe")
else()
    set(DXC "LD_LIBRARY_PATH=${WDC_DXC_ROOT}/lib/x64" "${WDC_DXC_ROOT}/bin/x64/dxc-linux")
endif()
set(DXC_COMMON_OPTS "-I${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src")
set(DXC_DXIL_OPTS "-Wno-ignored-attributes")
set(DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
set(DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T" "ps_6_3")
set(DXC_VS_OPTS "${DXC_COMMON_OPTS}" "-E" "VSMain" "-T" "vs_6_3" "-fvk-invert-y")
set(RECOMP_FRONTEND_N64MODERNRUNTIME_PATH "${N64MR}" CACHE PATH "" FORCE)
set(RECOMP_FRONTEND_RT64_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64" CACHE PATH "" FORCE)
if(ANDROID)
    set(sdl2_SOURCE_DIR "${WDC_ANDROID_DEPS}/SDL")
else()
    set(sdl2_SOURCE_DIR "${SDL2_WIN32_DEPS}")
endif()
add_subdirectory(lib/RecompFrontend)

target_link_libraries(recompui PUBLIC recompinput rt64)
target_link_libraries(recompinput PUBLIC recompui)
foreach(frontend_target recompui recompinput rmlui_core rmlui_debugger)
    if(TARGET ${frontend_target} AND NOT MSVC)
        target_compile_options(${frontend_target} PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:-include>"
            "$<$<COMPILE_LANGUAGE:CXX>:cstdint>")
    endif()
    if(TARGET ${frontend_target})
        target_include_directories(${frontend_target} PRIVATE ${SDL2_INCLUDE_DIRS})
    endif()
endforeach()
target_link_libraries(worlddriverchampionship_modern PRIVATE recompui recompinput)
target_compile_definitions(worlddriverchampionship_modern PRIVATE WDC_ENABLE_FRONTEND=1)
target_include_directories(worlddriverchampionship_modern PRIVATE src)
target_sources(worlddriverchampionship_modern PRIVATE src/ui/frontend.cpp src/ui/frontend_settings.cpp src/ui/sdl_host.cpp src/ui/input.cpp)

add_custom_command(TARGET worlddriverchampionship_modern POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/frontend"
        "$<TARGET_FILE_DIR:worlddriverchampionship_modern>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/RecompFrontend/recompui/lib/RmlUi/Samples/assets/LatoLatin-Regular.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/RecompFrontend/recompui/lib/RmlUi/Samples/assets/LatoLatin-Bold.ttf"
        "$<TARGET_FILE_DIR:worlddriverchampionship_modern>/assets")
if(WIN32)
    target_link_libraries(worlddriverchampionship_modern PRIVATE shell32)
    add_custom_command(TARGET worlddriverchampionship_modern POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/lib/RecompFrontend/recompui/lib/freetype-windows-binaries/release dll/win64/freetype.dll"
            "$<TARGET_FILE_DIR:worlddriverchampionship_modern>")
elseif(NOT ANDROID)
    target_link_libraries(recompui PRIVATE SDL2::SDL2)
endif()

if(BUILD_TESTING)
    add_executable(wdc_input_tests tests/test_input.cpp src/ui/input.cpp)
    add_executable(wdc_frontend_settings_tests tests/test_frontend_settings.cpp
        src/ui/frontend_settings.cpp src/graphics_config.cpp src/game_visibility.cpp)
    target_include_directories(wdc_frontend_settings_tests PRIVATE src ${SDL2_INCLUDE_DIRS})
    target_compile_definitions(wdc_frontend_settings_tests PRIVATE WDC_ENABLE_FRONTEND=1 SDL_MAIN_HANDLED NOMINMAX)
    target_link_libraries(wdc_frontend_settings_tests PRIVATE recompui recompinput librecomp ultramodern rt64 Threads::Threads ${SDL2_LIBRARIES})
    target_include_directories(wdc_input_tests PRIVATE src ${SDL2_INCLUDE_DIRS})
    target_compile_definitions(wdc_input_tests PRIVATE WDC_ENABLE_FRONTEND=1 SDL_MAIN_HANDLED NOMINMAX)
    target_link_libraries(wdc_input_tests PRIVATE recompui recompinput librecomp ultramodern rt64 Threads::Threads ${SDL2_LIBRARIES})
    if(WIN32)
        target_link_libraries(wdc_frontend_settings_tests PRIVATE shell32)
        target_link_libraries(wdc_input_tests PRIVATE shell32)
        if(MINGW)
            target_sources(wdc_frontend_settings_tests PRIVATE src/mingw_guids.cpp)
            target_sources(wdc_input_tests PRIVATE src/mingw_guids.cpp)
        endif()
    endif()
    add_test(NAME wdc_frontend_settings COMMAND wdc_frontend_settings_tests)
    add_test(NAME wdc_input COMMAND wdc_input_tests)
    set_tests_properties(wdc_input PROPERTIES TIMEOUT 30)
endif()
