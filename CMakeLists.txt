cmake_minimum_required(VERSION 3.29...4.2.3)

project(Babel VERSION 0.1.0 LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJC OBJCXX Swift)
    set(CMAKE_Swift_LANGUAGE_VERSION 6.1)
endif ()

# 设置语言标准
if (WIN32)
    set(CMAKE_C_STANDARD 23)
else ()
    set(CMAKE_C_STANDARD 23)
endif ()
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置开启显示详细的构建过程
set(CMAKE_VERBOSE_MAKEFILE ON)

# 启用测试
enable_testing()

# 添加包含和链接目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PROJECT_BINARY_DIR})
link_directories(${PROJECT_BINARY_DIR})

if (APPLE)
    set(CMAKE_MACOSX_RPATH 1)
    link_directories(${SWIFT_LIBRARY_SEARCH_PATHS})
    set(CMAKE_Swift_FLAGS "${CMAKE_Swift_FLAGS} -cxx-interoperability-mode=default")
    set(CMAKE_XCODE_ATTRIBUTE_XCODE_VERSION "26.0")
    set(CMAKE_Swift_LANGUAGE_VERSION 6)
    set(CMAKE_Swift_FLAGS "-strict-concurrency=complete")
endif ()

# 避免警告
if (POLICY CMP0012)
    cmake_policy(SET CMP0012 NEW)
endif ()
# 针对CMake 3.30版本Boost查找相关警告
if (POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif ()

# 引用头文件导出助手函数
include(GenerateExportHeader)

add_subdirectory(babel)
add_subdirectory(tests)
   