import Foundation
import os

/// 统一结构化日志封装（Apple os.Logger）
public enum BabelLogger {
    public static let subsystem = "com.babel.inputmethod"

    /// 输入法生命周期（激活、失活、模式切换）
    public static let lifecycle = Logger(subsystem: subsystem, category: "lifecycle")

    /// 按键拦截与判定（Key Down, Consume/Pass-through）
    public static let event = Logger(subsystem: subsystem, category: "event")

    /// 跨进程通信（UDS 连接状态、耗时、异常）
    public static let ipc = Logger(subsystem: subsystem, category: "ipc")

    /// 上屏提交事件
    public static let commit = Logger(subsystem: subsystem, category: "commit")
}

