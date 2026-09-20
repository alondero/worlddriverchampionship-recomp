# Android SDL2 is supplied as the SDL2::SDL2 CMake target by cmake/Android.cmake.
if(TARGET SDL2::SDL2)
    set(SDL2_FOUND TRUE)
    set(SDL2_LIBRARIES SDL2::SDL2)
endif()
