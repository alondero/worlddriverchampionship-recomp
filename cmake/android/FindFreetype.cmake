# Android FreeType discovery is supplied by the Android dependency preparation
# script. Keep this module small so it does not silently find a host library.
if(TARGET Freetype::Freetype)
    set(Freetype_FOUND TRUE)
endif()
