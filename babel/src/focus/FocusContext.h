#pragma once

#include <chrono>

// 平台相关窗口引用前置声明：Windows HWND / macOS 前台应用+窗口引用 / X11 Window ID
// 热键与注入模块均采用"分平台自研薄封装"（research.md §3/§4.2），
// 故此处类型即为各平台原生窗口引用类型本身，不引入第三方中间类型。
#if defined(_WIN32)
using BabelPlatformWindowRef = void *;  // HWND，避免在跨平台头文件中引入 <windows.h>
#elif defined(__APPLE__)
using BabelPlatformWindowRef = void *;  // 前台应用 pid_t + 窗口引用的不透明封装指针
#else
using BabelPlatformWindowRef = unsigned long;  // X11 Window（XID，typedef unsigned long）
#endif

namespace babel::focus {

/// 浮窗被唤起瞬间记录的、此前获得系统焦点的应用窗口信息（data-model.md §4）。
/// 用于确认后定位自动粘贴的目标位置（FR-001 记录、FR-008 使用）。
///
/// 刻意留白：目标窗口在浮窗唤起期间被切换/最小化/关闭后应如何表现，
/// 未被转化为正式 FR，本类型不定义该失效场景的降级行为（见 data-model.md §4 说明）。
struct TargetFocusContext {
  BabelPlatformWindowRef platformWindowRef = BabelPlatformWindowRef{};
  std::chrono::system_clock::time_point capturedAt{};
  bool valid = false;
};

/// 捕获当前系统前台焦点窗口，生成 TargetFocusContext（FR-001）。
/// 平台实现见 FocusContext.cpp（按 _WIN32/__APPLE__/X11 条件编译分支）。
TargetFocusContext captureCurrentFocus();

}  // namespace babel::focus
