set(APP_NAME Babel)

# 查找并引用Qt库
find_package(
        Qt6
        COMPONENTS Core
        Qml
        Quick
        Gui
        QuickControls2
        Widgets
        ShaderTools
        QuickDialogs2
        REQUIRED)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

qt_standard_project_setup()

file(GLOB_RECURSE SOURCES
        main.cpp )

qt_add_executable(${APP_NAME} MACOSX_BUNDLE WIN32 MANUAL_FINALIZATION)
target_sources(${APP_NAME} PUBLIC ${SOURCES})

target_link_libraries(
        ${APP_NAME}
        PRIVATE Qt6::Core
        Qt6::Qml
        Qt6::Quick
        Qt6::Gui
        Qt6::QuickControls2
        Qt6::Widgets
        Qt6::QuickDialogs2)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/content/sources)

file(GLOB_RECURSE ASSETS_SOURCES
        RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        content/*.png
        content/*.svg
        content/*.jpeg
        content/*.webp)

# 002-lattice-dict-model-switch（T002）：随包分发的 iDvel/rime-ice 拼音词库原始
# 数据文件（GPLv3，见 content/dictionaries/NOTICE-rime-ice.txt）连同许可证全文/
# 来源声明一并内嵌为 Qt 资源，供 babel/main.cpp 首次启动时部署到约定词库目录
# （FR-001「随包分发」、research.md §3）。零新增第三方依赖。
#
# bugfix: english-word-pinyin-misdetection —— 新增随包分发高频英文常用词表
# content/dictionaries/common-english-words.txt（来源：工作区既有
# temp/COCA60000.txt，用户已知情接受许可不确定性风险，见
# babel/src/fastpath/EnglishWordList.h 头部注释），供 FastCorrector 在拼音匹配
# 管线之前拦截合法英文常用词。
file(GLOB_RECURSE DICTIONARY_SOURCES
        RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        content/dictionaries/*.dict.yaml
        content/dictionaries/LICENSE-rime-ice.txt
        content/dictionaries/NOTICE-rime-ice.txt
        content/dictionaries/common-english-words.txt)
list(APPEND ASSETS_SOURCES ${DICTIONARY_SOURCES})

file(GLOB_RECURSE QUICK_SOURCES
        RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        content/*.qml)

qt_add_qml_module(
        ${APP_NAME}
        URI
        quick
        VERSION
        1.0
        RESOURCES
        ${ASSETS_SOURCES} 
        QML_FILES
        ${QUICK_SOURCES}) 
 
# 查找并引用日志库
find_package(spdlog CONFIG REQUIRED)
target_link_libraries(${APP_NAME} PUBLIC spdlog::spdlog)

# 查找并引用Boost库
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
find_package(Boost COMPONENTS system filesystem REQUIRED)
target_include_directories(${APP_NAME} PUBLIC ${Boost_INCLUDE_DIRS})
target_link_libraries(${APP_NAME} PUBLIC ${Boost_LIBRARIES})
 
# 查找并引用Sqlite解析库
find_package(unofficial-sqlite3 CONFIG REQUIRED)
target_link_libraries(${APP_NAME} PUBLIC unofficial::sqlite3::sqlite3)

# 引用本特性（001-floating-ime-converter）新增的 babel_core 业务逻辑静态库
add_subdirectory(src)
target_link_libraries(${APP_NAME} PUBLIC babel_core)

# 完成 Qt 目标配置
qt_finalize_target(${APP_NAME})
