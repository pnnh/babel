import Foundation
import Carbon

guard CommandLine.arguments.count > 1 else {
    fputs("Usage: tis_register <path-to-bundle>\n", stderr)
    exit(1)
}

let bundlePath = CommandLine.arguments[1]
let url = URL(fileURLWithPath: bundlePath)

print("==> 1. 验证目标 Bundle 可读性与 CFBundle 解析...")
if let bundle = Bundle(url: url) {
    print("    BundleIdentifier: \(bundle.bundleIdentifier ?? "nil")")
    print("    ExecutablePath: \(bundle.executablePath ?? "nil")")
} else {
    print("    ⚠ Bundle(url:) 解析失败！")
}

print("==> 2. 调用 TISRegisterInputSource 注册 Bundle: \(url.path)")
let regStatus = TISRegisterInputSource(url as CFURL)
print("    TISRegisterInputSource 返回状态: \(regStatus)")

Thread.sleep(forTimeInterval: 0.3)

print("==> 3. 检索系统输入源 (includeAllInstalled: true)...")
if let list = TISCreateInputSourceList(nil, true)?.takeRetainedValue() as? [TISInputSource] {
    var foundSources: [TISInputSource] = []
    var sampleSource: TISInputSource?

    for s in list {
        if let idPtr = TISGetInputSourceProperty(s, kTISPropertyInputSourceID) {
            let id = Unmanaged<CFString>.fromOpaque(idPtr).takeUnretainedValue() as String
            if id.contains("babel") || id.contains("Babel") {
                foundSources.append(s)
                let namePtr = TISGetInputSourceProperty(s, kTISPropertyLocalizedName)
                let name = namePtr != nil ? Unmanaged<CFString>.fromOpaque(namePtr!).takeUnretainedValue() as String : "nil"
                print("    ✓ 成功发现 Babel 输入源: ID=\(id), Name=\(name)")
            } else if id == "com.bytedance.inputmethod.doubaoime.pinyin" {
                sampleSource = s
            }
        }
    }

    if let s = sampleSource {
        print("\n=== [系统参照对象] 豆包输入法属性透视 ===")
        let propKeys: [(String, CFString)] = [
            ("Type", kTISPropertyInputSourceType),
            ("Category", kTISPropertyInputSourceCategory),
            ("BundleID", kTISPropertyBundleID),
            ("ModeID", kTISPropertyInputModeID),
            ("LocalizedName", kTISPropertyLocalizedName),
            ("IsSelectCapable", kTISPropertyInputSourceIsSelectCapable),
            ("IsEnabled", kTISPropertyInputSourceIsEnabled)
        ]
        for (label, k) in propKeys {
            if let ptr = TISGetInputSourceProperty(s, k) {
                let obj = Unmanaged<AnyObject>.fromOpaque(ptr).takeUnretainedValue()
                print("    \(label): \(obj)")
            } else {
                print("    \(label): nil")
            }
        }
        print("=========================================\n")
    }

    if !foundSources.isEmpty {
        // 先启用父级 Bundle (TISTypeKeyboardInputMethodBundle)，再启用并选中子级 Mode (TISTypeKeyboardInputMode)
        for source in foundSources {
            let idPtr = TISGetInputSourceProperty(source, kTISPropertyInputSourceID)
            let id = idPtr != nil ? Unmanaged<CFString>.fromOpaque(idPtr!).takeUnretainedValue() as String : "unknown"
            print("==> 4. 正在启用输入源 (TISEnableInputSource): \(id)...")
            let enStatus = TISEnableInputSource(source)
            print("    TISEnableInputSource(\(id)) 状态: \(enStatus)")
        }
        for source in foundSources {
            if let isSelectCapablePtr = TISGetInputSourceProperty(source, kTISPropertyInputSourceIsSelectCapable) {
                let isSelectCapable = Unmanaged<CFBoolean>.fromOpaque(isSelectCapablePtr).takeUnretainedValue()
                if CFBooleanGetValue(isSelectCapable) {
                    let idPtr = TISGetInputSourceProperty(source, kTISPropertyInputSourceID)
                    let id = idPtr != nil ? Unmanaged<CFString>.fromOpaque(idPtr!).takeUnretainedValue() as String : "unknown"
                    print("==> 5. 正在设为当前活跃输入法 (TISSelectInputSource): \(id)...")
                    let selStatus = TISSelectInputSource(source)
                    print("    TISSelectInputSource(\(id)) 状态: \(selStatus)")
                }
            }
        }
    } else {
        print("    ℹ 系统 TIS 列表未收录 Babel。正在按 BundleID 查询...")
        let qProps: [CFString: Any] = [
            kTISPropertyBundleID: "com.babel.inputmethod.Babel"
        ]
        if let bList = TISCreateInputSourceList(qProps as CFDictionary, true)?.takeRetainedValue() as? [TISInputSource], !bList.isEmpty {
            print("    ✓ 按 BundleID 找到了 \(bList.count) 个输入源！")
        } else {
            print("    按 BundleID 亦未找到。")
        }
    }
} else {
    print("    ⚠ 获取系统输入源列表失败。")
}
