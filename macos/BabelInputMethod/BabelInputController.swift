import Cocoa
import InputMethodKit

@objc(BabelInputController)
public class BabelInputController: IMKInputController {
    private let ipcClient = IpcClient()

    override public func handle(_ event: NSEvent!, client sender: Any!) -> Bool {
        guard let event = event, event.type == .keyDown else {
            return false
        }

        // 安全输入检测：若系统开启安全输入（如密码输入框），强制直通
        if IsSecureEventInputEnabled() {
            BabelLogger.event.info("SecureEventInput enabled, passing through keystroke")
            return false
        }

        let keyCode = Int(event.keyCode)
        let characters = event.characters ?? ""
        let modifiers = Int(event.modifierFlags.rawValue)

        BabelLogger.event.debug("handleEvent: keyCode=\(keyCode) chars=\(characters)")

        guard let result = ipcClient.sendKeyDown(keyCode: keyCode, characters: characters, modifiers: modifiers) else {
            // IPC 不可达时自动无感降级为直通英文，严禁丢键
            BabelLogger.event.info("IPC unreachable, passing through")
            return false
        }

        guard let client = sender as? (IMKTextInput & NSObjectProtocol) else {
            return result.consumed
        }

        if result.action == "UPDATE_PREEDIT" {
            let markedString = NSAttributedString(
                string: result.markedText,
                attributes: [
                    .underlineStyle: NSUnderlineStyle.single.rawValue,
                ]
            )
            client.setMarkedText(
                markedString,
                selectionRange: NSRange(location: result.markedText.count, length: 0),
                replacementRange: NSRange(location: NSNotFound, length: NSNotFound)
            )
            BabelLogger.event.debug("Updated markedText: \(result.markedText)")
        } else if result.action == "COMMIT_TEXT" {
            // 原子化上屏最终文本，无需剪贴板中转
            client.insertText(result.commitText, replacementRange: NSRange(location: NSNotFound, length: NSNotFound))
            BabelLogger.commit.info("Committed text via insertText: \(result.commitText)")
        }

        return result.consumed
    }

    override public func activateServer(_ sender: Any!) {
        super.activateServer(sender)
        BabelLogger.lifecycle.info("activateServer called")
        _ = ipcClient.connectIfNeeded()
    }

    override public func deactivateServer(_ sender: Any!) {
        BabelLogger.lifecycle.info("deactivateServer called")
        super.deactivateServer(sender)
    }
}

