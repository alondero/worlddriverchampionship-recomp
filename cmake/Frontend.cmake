# RecompFrontend is enabled once the first host/runtime window exists. Keeping
# it behind WDC_ENABLE_FRONTEND lets source-only bootstrap checks run without
# pretending that the WDC UI contract is already known.
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

add_custom_command(TARGET worlddriverchampionship_modern POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/frontend"
        "$<TARGET_FILE_DIR:worlddriverchampionship_modern>/assets")
