# tests：本特性（001-floating-ime-converter）新增的 GoogleTest 单元测试目录
# 结构镜像 babel/src/ 子目录划分；每个 *Test.cpp 文件注册为独立的 CTest 可执行目标

find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

file(GLOB_RECURSE BABEL_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*Test.cpp)

foreach (test_src ${BABEL_TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE babel_core GTest::gtest GTest::gtest_main)
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    gtest_discover_tests(${test_name})
endforeach ()
