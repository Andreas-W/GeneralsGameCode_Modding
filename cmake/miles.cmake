# x64 uses only the header: core_milesminiaudio implements the functions and takes the name milesstub.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(MILES_SOURCE_SUBDIR do-not-add-miles-stub)
else()
    set(MILES_SOURCE_SUBDIR .)
endif()

FetchContent_Declare(
    miles
    GIT_REPOSITORY https://github.com/TheSuperHackers/miles-sdk-stub.git
    GIT_TAG        6e32700d7ba4b4713a03bf1f5ffc3b0ac8d17264
    SOURCE_SUBDIR  ${MILES_SOURCE_SUBDIR}
)

FetchContent_MakeAvailable(miles)
