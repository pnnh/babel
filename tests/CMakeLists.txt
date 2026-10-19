# tests：本特性（001-floating-ime-converter）新增的 GoogleTest 单元测试目录
# 结构镜像 babel/src/ 子目录划分；每个 *Test.cpp 文件注册为独立的 CTest 可执行目标

find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

# 本机 macOS 沙箱环境已知限制：未签名的、链接 AppKit/Carbon/ApplicationServices
# 框架的可执行文件会在执行时被沙箱随机/确定性 SIGKILL（002 任务说明已记录，
# 001 实施已验证 ad-hoc codesign 后可正常运行）。默认 DISCOVERY_MODE=POST_BUILD
# 会在构建期立即执行刚链接出的测试二进制以枚举用例，此时尚未签名，必然触发该
# 限制；改为 PRE_TEST 使枚举推迟到 `ctest` 首次运行时才执行，并为每个测试目标
# 追加一个 POST_BUILD ad-hoc 签名步骤（`codesign --force --sign -`），使枚举/运行
# 阶段均作用于已签名的二进制。纯本地构建基础设施健壮性修复，不改变任何测试用
# 例的逻辑或断言。
set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

file(GLOB_RECURSE BABEL_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*Test.cpp)

foreach (test_src ${BABEL_TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE babel_core GTest::gtest GTest::gtest_main)
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if (APPLE)
        add_custom_command(TARGET ${test_name} POST_BUILD
                COMMAND codesign --force --sign - "$<TARGET_FILE:${test_name}>"
                COMMENT "Ad-hoc codesign ${test_name} (沙箱未签名可执行文件随机 SIGKILL 限制)")
    endif ()
    gtest_discover_tests(${test_name})
endforeach ()
