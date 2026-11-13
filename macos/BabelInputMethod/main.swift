import Cocoa
import InputMethodKit

@main
final class AppDelegate: NSObject, NSApplicationDelegate {
    static var server: IMKServer?

    static func main() {
        let app = NSApplication.shared
        let delegate = AppDelegate()
        app.delegate = delegate

        let connectionName =
            Bundle.main.infoDictionary?["InputMethodConnectionName"] as? String ?? "com.babel.inputmethod.Babel_Connection"
        let bundleID = Bundle.main.bundleIdentifier ?? "com.babel.inputmethod.Babel"

        BabelLogger.lifecycle.info("Starting IMKServer at main startup: name=\(connectionName) bundleId=\(bundleID)")
        server = IMKServer(name: connectionName, bundleIdentifier: bundleID)

        app.run()
    }
}
