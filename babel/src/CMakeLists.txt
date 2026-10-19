# babel_core：本特性（001-floating-ime-converter）新增的纯 C++ 业务逻辑静态库
# 按 fastpath/slowpath/hotkey/inject/focus/session 六个子目录划分，见 plan.md Project Structure

file(GLOB_RECURSE BABEL_CORE_SOURCES
        CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

# 按平台过滤分平台源文件（文件名约定：_win/_mac(.mm)/_x11 后缀）
set(BABEL_CORE_PLATFORM_SOURCES "")
foreach (src_file ${BABEL_CORE_SOURCES})
    if (src_file MATCHES "_win\\.cpp$")
        if (WIN32)
            list(APPEND BABEL_CORE_PLATFORM_SOURCES ${src_file})
        endif ()
    elseif (src_file MATCHES "_x11\\.cpp$")
        if (UNIX AND NOT APPLE)
            list(APPEND BABEL_CORE_PLATFORM_SOURCES ${src_file})
        endif ()
    else ()
        list(APPEND BABEL_CORE_PLATFORM_SOURCES ${src_file})
    endif ()
endforeach ()

if (APPLE)
    file(GLOB_RECURSE BABEL_CORE_MM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.mm)
    list(APPEND BABEL_CORE_PLATFORM_SOURCES ${BABEL_CORE_MM_SOURCES})
endif ()

add_library(babel_core STATIC ${BABEL_CORE_PLATFORM_SOURCES})

target_include_directories(babel_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Qt6 COMPONENTS Core Gui REQUIRED)
target_link_libraries(babel_core PUBLIC Qt6::Core Qt6::Gui)

find_package(unofficial-sqlite3 CONFIG REQUIRED)
target_link_libraries(babel_core PUBLIC unofficial::sqlite3::sqlite3)

find_package(spdlog CONFIG REQUIRED)
target_link_libraries(babel_core PUBLIC spdlog::spdlog)

find_package(llama CONFIG QUIET)
if (llama_FOUND)
    target_link_libraries(babel_core PUBLIC llama)
    target_compile_definitions(babel_core PUBLIC BABEL_HAVE_LLAMA_CPP=1)
else ()
    message(WARNING "llama-cpp 未找到：慢流 LlmEngine 将以 modelAvailable=false 降级编译（对应 FR-011）")
    target_compile_definitions(babel_core PUBLIC BABEL_HAVE_LLAMA_CPP=0)
endif ()

if (APPLE)
    target_link_libraries(babel_core PUBLIC "-framework ApplicationServices" "-framework Carbon" "-framework AppKit")
endif ()

if (UNIX AND NOT APPLE)
    find_package(X11)
    if (X11_FOUND)
        target_link_libraries(babel_core PUBLIC ${X11_LIBRARIES} ${X11_Xtst_LIB})
        target_compile_definitions(babel_core PUBLIC BABEL_HAVE_X11=1)
    else ()
        target_compile_definitions(babel_core PUBLIC BABEL_HAVE_X11=0)
    endif ()
endif ()
