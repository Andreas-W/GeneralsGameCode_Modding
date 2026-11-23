# Fetch miniaudio (public domain / MIT-0), the audio backend of x64 builds.
# Only the header is used: the audio device compiles the implementation itself
# (MINIAUDIO_IMPLEMENTATION), so miniaudio's own CMake project with its examples and tests is skipped.

include(FetchContent)
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        877ea4a7c2601c7b0f2a44e83dfac42e6470fea6 # 0.11.26
    SOURCE_SUBDIR  do-not-add-miniaudio-project
)

FetchContent_MakeAvailable(miniaudio)

add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${miniaudio_SOURCE_DIR})
