import Cocoa
import InputMethodKit

/// IMKServer 生命周期管理
public final class BabelServer {
    public static let shared = BabelServer()
    private var server: IMKServer?

    private init() {}

    public func start() {
        let connectionName =
            Bundle.main.infoDictionary?["InputMethodConnectionName"] as? String ?? "Babel_1_Connection_Name"
        let bundleIdentifier = Bundle.main.bundleIdentifier ?? "com.babel.inputmethod"

        BabelLogger.lifecycle.info("Starting IMKServer: name=\(connectionName) bundleId=\(bundleIdentifier)")
        self.server = IMKServer(name: connectionName, bundleIdentifier: bundleIdentifier)
    }
}

