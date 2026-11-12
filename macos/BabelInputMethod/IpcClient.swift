import Darwin
import Foundation

/// Unix Domain Socket 低延迟 IPC 客户端
public final class IpcClient {
    private var socketFd: Int32 = -1
    private var seq: UInt64 = 0
    private let socketPath: String

    public init(customPath: String? = nil) {
        if let customPath = customPath {
            self.socketPath = customPath
        } else {
            let home = ProcessInfo.processInfo.environment["HOME"] ?? ""
            self.socketPath = home.isEmpty ? "/tmp/babel.sock" : "\(home)/.local/run/babel.sock"
        }
    }

    deinit {
        disconnect()
    }

    public func isConnected() -> Bool {
        return socketFd >= 0
    }

    public func connectIfNeeded() -> Bool {
        if isConnected() { return true }

        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        guard fd >= 0 else {
            BabelLogger.ipc.error("Failed to create socket: \(errno)")
            return false
        }

        // 设置读写超时 10ms，保证输入法主线程绝不被阻塞
        var timeout = timeval(tv_sec: 0, tv_usec: 10_000)
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))

        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)

        let maxLen = MemoryLayout.size(ofValue: addr.sun_path)
        guard socketPath.utf8.count < maxLen else {
            BabelLogger.ipc.error("Socket path too long: \(self.socketPath)")
            close(fd)
            return false
        }

        withUnsafeMutablePointer(to: &addr.sun_path.0) { ptr in
            _ = socketPath.withCString { cstr in
                strncpy(ptr, cstr, maxLen)
            }
        }

        let addrLen = socklen_t(MemoryLayout<sockaddr_un>.size)
        let res = withUnsafePointer(to: &addr) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sockPtr in
                Darwin.connect(fd, sockPtr, addrLen)
            }
        }

        guard res == 0 else {
            close(fd)
            return false
        }

        self.socketFd = fd
        BabelLogger.ipc.info("Connected to Unix Domain Socket: \(self.socketPath)")
        return true
    }

    public func disconnect() {
        if socketFd >= 0 {
            close(socketFd)
            socketFd = -1
            BabelLogger.ipc.info("Disconnected from Unix Domain Socket")
        }
    }

    public func sendKeyDown(keyCode: Int, characters: String, modifiers: Int)
        -> (consumed: Bool, action: String, markedText: String, commitText: String)?
    {
        guard connectIfNeeded() else {
            return nil
        }

        seq += 1
        let payload: [String: Any] = [
            "keyCode": keyCode,
            "characters": characters,
            "modifiers": modifiers,
        ]
        let envelope: [String: Any] = [
            "seq": seq,
            "type": "KEY_DOWN",
            "payload": payload,
        ]

        guard let jsonData = try? JSONSerialization.data(withJSONObject: envelope) else {
            return nil
        }

        var header = UInt32(jsonData.count).bigEndian
        let startTime = DispatchTime.now()

        // 发送 4 字节头部 + JSON
        let writeHeader = Darwin.write(socketFd, &header, 4)
        guard writeHeader == 4 else {
            disconnect()
            return nil
        }

        let writeBody = jsonData.withUnsafeBytes { ptr in
            Darwin.write(socketFd, ptr.baseAddress, jsonData.count)
        }
        guard writeBody == jsonData.count else {
            disconnect()
            return nil
        }

        // 读取 4 字节响应头部
        var respLenBig: UInt32 = 0
        let readHeader = Darwin.read(socketFd, &respLenBig, 4)
        guard readHeader == 4 else {
            disconnect()
            return nil
        }

        let respLen = Int(UInt32(bigEndian: respLenBig))
        guard respLen > 0 && respLen <= 1024 * 1024 else {
            disconnect()
            return nil
        }

        var buffer = [UInt8](repeating: 0, count: respLen)
        var totalRead = 0
        while totalRead < respLen {
            let n = Darwin.read(socketFd, &buffer[totalRead], respLen - totalRead)
            guard n > 0 else {
                disconnect()
                return nil
            }
            totalRead += n
        }

        let endTime = DispatchTime.now()
        let elapsedUs = Double(endTime.uptimeNanoseconds - startTime.uptimeNanoseconds) / 1000.0
        BabelLogger.ipc.debug("[IPC RTT] seq=\(self.seq) elapsed=\(elapsedUs)µs")

        let respData = Data(buffer)
        guard let obj = (try? JSONSerialization.jsonObject(with: respData)) as? [String: Any],
              let payloadObj = obj["payload"] as? [String: Any]
        else {
            return nil
        }

        let consumed = (payloadObj["consumed"] as? Bool) ?? false
        let action = (payloadObj["action"] as? String) ?? "NONE"
        let markedText = (payloadObj["markedText"] as? String) ?? ""
        let commitText = (payloadObj["commitText"] as? String) ?? ""

        return (consumed, action, markedText, commitText)
    }
}

