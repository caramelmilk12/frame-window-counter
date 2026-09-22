cmake_minimum_required(VERSION 3.25)

# ==========================================
# 1. 项目基础配置
# ==========================================
project(frame_window_counter)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==========================================
# 2. Geode v5 与 Arc 异步运行时配置
# ==========================================
set(ARC_FEATURE_FULL ON CACHE BOOL "" FORCE)

if(NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
endif()
add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# ==========================================
# 3. CPM 包管理器初始化
# ==========================================
set(CPM_DOWNLOAD_VERSION 0.40.2)
if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
    message(STATUS "Downloading CPM.cmake...")
    file(DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake"
        STATUS DOWNLOAD_STATUS
    )
    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    if(NOT STATUS_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to download CPM.cmake: ${DOWNLOAD_STATUS}")
    endif()
endif()
include("${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")

# ==========================================
# 4. 目标与源文件定义
# ==========================================
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.hpp"
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

setup_geode_mod(${PROJECT_NAME})

# ==========================================
# 5. 第三方依赖项管理
# ==========================================

# --- 依赖 A: Silicate (slc) 宏格式库 ---
include(FetchContent)
FetchContent_Declare(
    slc_repo
    GIT_REPOSITORY https://git.puppy.lgbt/silicate/slc.git
    GIT_TAG        main
)
FetchContent_GetProperties(slc_repo)
if(NOT slc_repo_POPULATED)
    FetchContent_Populate(slc_repo)
endif()
target_include_directories(${PROJECT_NAME} PRIVATE ${slc_repo_SOURCE_DIR}/include)

# --- 依赖 B: GDR 格式及其转换器 ---
CPMAddPackage("gh:maxnut/GDReplayFormat#2a0ca3c")

CPMAddPackage(
    NAME msgpack-for-matjson
    GITHUB_REPOSITORY prevter/msgpack-for-matjson
    GIT_TAG main
)

CPMAddPackage(
    NAME GDRconverter
    GITHUB_REPOSITORY maxnut/GDR-converter
    GIT_TAG main
    EXCLUDE_FROM_ALL YES
    OPTIONS "BUILD_TESTING OFF"
)

# ==========================================
# 6. 链接库
# ==========================================
target_link_libraries(${PROJECT_NAME} libGDR GDRconverter)