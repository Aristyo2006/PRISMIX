include(FetchContent)

FetchContent_Declare(
    SDL2_prebuilt
    URL      https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-VC.zip
    URL_HASH SHA256=AF347939395A58B365846AAEA27391E69F9EC9D4DD650D6AC40802159B418A6E
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(SDL2_prebuilt)

set(SDL2_PREBUILT_ROOT "${sdl2_prebuilt_SOURCE_DIR}")
set(SDL2_INCLUDE_DIR "${SDL2_PREBUILT_ROOT}/include")
set(SDL2_LIB_DIR "${SDL2_PREBUILT_ROOT}/lib/x64")

# We will create an imported target for SDL2
if(NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 UNKNOWN IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        IMPORTED_LOCATION "${SDL2_LIB_DIR}/SDL2.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}"
    )
endif()

if(NOT TARGET SDL2::SDL2main)
    add_library(SDL2::SDL2main UNKNOWN IMPORTED)
    set_target_properties(SDL2::SDL2main PROPERTIES
        IMPORTED_LOCATION "${SDL2_LIB_DIR}/SDL2main.lib"
    )
endif()

# We need the DLL path to copy it post-build
set(SDL2_DLL_PATH "${SDL2_LIB_DIR}/SDL2.dll" CACHE INTERNAL "Path to SDL2.dll")
