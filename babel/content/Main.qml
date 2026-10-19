import QtQuick

// 浮窗转换器（001-floating-ime-converter）为纯后台工具应用：无持久可见主窗口，
// 仅由全局热键触发 SessionController.sessionInvoked() 时显示浮窗（FR-001）。
// 原脚手架占位的大尺寸主窗口已移除——其与 FloatingWindow 构成 QML 嵌套 Window 的
// 瞬态父子关系会导致浮窗被固定显示在该主窗口附近，而非"在目标应用前景弹出"
// （spec.md Acceptance Scenario 1），故 FloatingWindow 直接作为本模块唯一根窗口。
FloatingWindow {
    id: floatingWindow
}

