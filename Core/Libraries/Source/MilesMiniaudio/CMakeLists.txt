# The Miles Sound System functions the game's MilesAudioManager uses, implemented on miniaudio.
# x64 builds link this in place of mss32.dll, which exists only for 32-bit.
# Declarations come from the Miles SDK stub header (mss/mss.h); BUILD_STUBS makes them plain
# functions instead of DLL imports.

add_library(core_milesminiaudio STATIC)
set_target_properties(core_milesminiaudio PROPERTIES OUTPUT_NAME milesminiaudio)

target_sources(core_milesminiaudio PRIVATE
    MilesMiniaudio.cpp
    MiniaudioImplementation.c
)

target_include_directories(core_milesminiaudio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${miles_SOURCE_DIR}
    ${miles_SOURCE_DIR}/mss
)

target_compile_definitions(core_milesminiaudio
    PUBLIC
        BUILD_STUBS
    PRIVATE
        MA_NO_ENCODING
)

target_link_libraries(core_milesminiaudio PRIVATE
    miniaudio
)

# Everything that links the Miles stub links this instead.
add_library(milesstub ALIAS core_milesminiaudio)
