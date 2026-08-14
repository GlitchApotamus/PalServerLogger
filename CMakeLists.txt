cmake_minimum_required(VERSION 3.20)
project(PalServerLogger VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Version from source-of-truth file
set(VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/version.txt")

if(EXISTS "${VERSION_FILE}")
    file(READ "${VERSION_FILE}" VERSION_CONTENT)
    string(STRIP "${VERSION_CONTENT}" VERSION_CONTENT)
    string(REPLACE "." ";" VERSION_LIST "${VERSION_CONTENT}")
    list(LENGTH VERSION_LIST VERSION_LEN)

    if(VERSION_LEN GREATER 1)
        list(GET VERSION_LIST 0 CPACK_MINOR_VAL)
        list(GET VERSION_LIST 1 CPACK_PATCH_VAL)
    else()
        set(CPACK_MINOR_VAL 0)
        set(CPACK_PATCH_VAL 0)
    endif()
else()
    set(CPACK_MINOR_VAL 0)
    set(CPACK_PATCH_VAL 0)
endif()

string(TIMESTAMP BUILD_TIMESTAMP "%Y%m%d_%H%M%S")
set(PACKAGE_VERSION "1.${CPACK_MINOR_VAL}.${CPACK_PATCH_VAL}-${BUILD_TIMESTAMP}")

# 2. Fetch Dependencies
include(FetchContent)

FetchContent_Declare(minhook GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git GIT_TAG master)
FetchContent_MakeAvailable(minhook)

FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3)
FetchContent_MakeAvailable(json)

# 3. Target 1: The Logger Logic
add_library(PalServerLogger SHARED src/dllmain.cpp)
target_link_libraries(PalServerLogger PRIVATE minhook nlohmann_json::nlohmann_json)
target_include_directories(PalServerLogger PRIVATE ${minhook_SOURCE_DIR}/include)

# 4. Target 2: The d3d9 Proxy Chain-Loader
add_library(ProxyLoader SHARED src/loader.cpp)
target_link_libraries(ProxyLoader PRIVATE nlohmann_json::nlohmann_json)
set_target_properties(ProxyLoader PROPERTIES PREFIX "" OUTPUT_NAME "d3d9")

# 5. Automated Versioning and Packaging Post-Build Script
add_custom_command(
    TARGET ProxyLoader POST_BUILD
    COMMAND powershell -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/package.ps1"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Release"
    COMMENT "Auto-incrementing version and packaging ZIP..."
)