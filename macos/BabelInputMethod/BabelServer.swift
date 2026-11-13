import Cocoa
import InputMethodKit

/// IMKServer 状态辅助查询
public final class BabelServer {
    public static let shared = BabelServer()

    private init() {}

    public var server: IMKServer? {
        return AppDelegate.server
    }
}
