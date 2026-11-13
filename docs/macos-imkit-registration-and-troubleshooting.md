# macOS IMKit 原生输入法注册、加载机制与排错指南

---

## 1. 概述

Babel macOS 系统级输入法在演进方案（见 [macos-ime-evolution-plan.md](file:///Users/Larry/Projects/pnnh/babel/docs/plan/macos-ime-evolution-plan.md)）中采用 **Frontend Stub + Core Daemon** 分离架构：

- **前端宿主（`Babel.app`）**：基于 Swift 6 与 `InputMethodKit`（`IMKServer` / `IMKInputController`），部署于 `/Library/Input Methods/Babel.app`（或 `~/Library/Input Methods/Babel.app`），负责拦截击键事件、渲染系统输入法菜单与预编辑文本、通过原生 API 上屏。
- **后端守护进程（`Babel --daemon`）**：基于 C++20 与 Qt 6（仅依赖 Core/Network 等无界面模块），通过 Unix Domain Socket（`~/.local/run/babel.sock`）为前端提供低延迟拼音与大模型推理能力。

在 macOS 上，一个 `.app` Bundle 要被系统「键盘 -> 输入源」识别、启用并由 WindowServer 正常拉起，必须同时通过 **LaunchServices**、**Carbon Text Input Sources（TIS）** 以及 **`HIToolbox.framework` 内部缓存扫描器** 的多重校验。本文总结了通过 `lldb` 反汇编系统框架验证的底层准入机制、标准配置清单及开发调试期的所有踩坑记录。

---

## 2. 底层机制揭秘：为什么输入源会被系统静默过滤（`HIToolbox` 反汇编实证）

在排查输入法无法出现在「系统设置 -> 键盘 -> 输入源」的问题时，最容易被误导的现象是：**调用 `TISRegisterInputSource` 返回 `0`（`noErr`），但输入法依然不在系统列表中**。通过 `lldb` 对 `/System/Library/Frameworks/Carbon.framework/Frameworks/HIToolbox.framework/HIToolbox` 进行反汇编，可以还原苹果底层的真实逻辑。

### 2.1 `TISRegisterInputSource` 的真实行为

反汇编 `HIToolbox` 中的 `TISRegisterInputSource` 与 `RebuildInputMethodCache`：

```assembly
HIToolbox`TISRegisterInputSource:
    mov  x0, #0x0
    bl   TSMRegisterTextServiceInstall ; 内部跳转至 RebuildInputMethodCache -> islcRebuildInputSourceCache(2)
    mov  w0, #0x0                      ; 无条件返回 0 (noErr)！
    ret
```

**关键结论**：
1. 在现代 macOS 上，`TISRegisterInputSource(locationURL)` **忽略传入的 `CFURLRef` 参数**，它只做一件事：触发全局输入源缓存重建（`islcRebuildInputSourceCache`）。
2. 该函数**永远无条件返回 `0`（`noErr`）**。返回 `0` 仅意味着“已触发缓存重建流程”，**绝不代表目标 `.app` Bundle 通过了系统的合法性校验或已被写入缓存**。

### 2.2 `TISFileInterrogator` 的硬性准入条件

在重建输入源缓存时，`HIToolbox` 会调用内部函数 `TISFileInterrogator`（地址区间 `HIToolbox[0x18ef26164 - 0x18ef26304]`）逐个扫描 `/Library/Input Methods/`、`/System/Library/Input Methods/` 与 `~/Library/Input Methods/` 中的 `.app` Bundle。

反汇编还原其对 `.app` Bundle 的核心筛选逻辑如下：

```c
// HIToolbox`TISFileInterrogator 核心校验逻辑（反汇编还原）
CFStringRef bundlePath = CFURLCopyFileSystemPath(bundleURL, kCFURLPOSIXPathStyle);

// 1. 路径必须包含 "/Input Methods/" (常量地址 0x18f10902b)
if (CFStringFind(bundlePath, CFSTR("/Input Methods/"), 0).length < 1) {
    goto skip;
}

// 2. 必须能够读取 CFBundleIdentifier
CFStringRef bundleID = CFBundleGetIdentifier(bundle);
if (bundleID == NULL) {
    goto skip; // 开启调试日志时会打印 CFBundleGetIdentifier FAILED
}

// 3. 检查 Info.plist 中的 "InputMethodType" (0x18f10903b) 或 BundleID 子串 ".inputmethod." (0x18f10904b)
CFTypeRef imType = CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("InputMethodType"));
CFRange match = CFStringFind(bundleID, CFSTR(".inputmethod."), 0); // <--- 致命卡点！

if (match.length <= 0 && imType == NULL) {
    // 既不是旧式 InputMethodType（如 palette/ink），BundleID 又不包含 ".inputmethod."
    // 直接静默跳过该 Bundle，不报错也不写入 TIS 缓存！
    goto skip;
}

// 4. 通过校验后，调用 AddInputMethodAppInfoForCache 解析 Info.plist 与输入模式
```

> [!IMPORTANT]
> **核心铁律**：标准键盘输入法的 `CFBundleIdentifier` **必须包含 `".inputmethod."` 子串（前后都必须带有英文句点 `.`）**，即至少 4 段式结构 `com.<vendor>.inputmethod.<ProductName>`。
> 若写成 3 段式 `com.babel.inputmethod`（`.inputmethod` 位于末尾，缺少第二个点号），`CFStringFind(..., ".inputmethod.", 0)` 将返回 `length = 0`，`TISFileInterrogator` 会**静默丢弃**该输入法，**即使注销或重启系统也永远不会出现在输入源列表中**。

### 2.3 系统中第三方与开源输入法命名实测对照

使用 [tis_register.swift](file:///Users/Larry/Projects/pnnh/babel/macos/tis_register.swift) 读取系统中已收录的第三方输入法及开源实现，均严格遵循该规则：

| 输入法 | `CFBundleIdentifier` | 输入模式 `TISInputSourceID` | 是否包含 `.inputmethod.` |
| :--- | :--- | :--- | :--- |
| 搜狗输入法 | `com.sogou.inputmethod.sogou` | `com.sogou.inputmethod.sogou.pinyin` | 是 |
| 百度输入法 | `com.baidu.inputmethod.BaiduIM` | `com.baidu.inputmethod.BaiduIM.pinyin` | 是 |
| 豆包输入法 | `com.bytedance.inputmethod.doubaoime` | `com.bytedance.inputmethod.doubaoime.pinyin` | 是 |
| 千问输入法 | `com.qianwen.inputmethod.desktopime` | `com.qianwen.inputmethod.desktopime.pinyin` | 是 |
| 鼠须管 (Rime) | `im.rime.inputmethod.Squirrel` | `im.rime.inputmethod.Squirrel.Hans` | 是 |
| **Babel (错误旧值)** | `com.babel.inputmethod` | `com.babel.inputmethod.pinyin` | **否（被静默过滤）** |
| **Babel (修复后)** | `com.babel.inputmethod.Babel` | `com.babel.inputmethod.Babel.pinyin` | **是（成功收录）** |

---

## 3. macOS 输入法 Bundle 标准配置规范（Checklist）

### 3.1 目录结构

部署到 `/Library/Input Methods/Babel.app`（系统级）或 `~/Library/Input Methods/Babel.app`（用户级）的完整结构如下：

```text
Babel.app/
└── Contents/
    ├── Info.plist
    ├── PkgInfo                         # 固定 8 字节内容: "APPL????"
    ├── _CodeSignature/                 # codesign --force --deep --sign - 生成
    ├── MacOS/
    │   └── Babel                       # 与 CFBundleExecutable 一致
    └── Resources/
        ├── icon.pdf                    # 16x16 矢量 PDF 菜单栏图标
        ├── InfoPlist.strings           # 根目录本地化兜底文件
        ├── zh-Hans.lproj/
        │   └── InfoPlist.strings       # 简体中文输入法与输入模式显示名
        └── en.lproj/
            └── InfoPlist.strings       # 英文输入法与输入模式显示名
```

### 3.2 `Info.plist` 必填核心键值

参考 [Info.plist](file:///Users/Larry/Projects/pnnh/babel/macos/BabelInputMethod/Info.plist)：

1. **Bundle 基础标识**：
   - `CFBundleIdentifier`：`com.babel.inputmethod.Babel`（必须包含 `.inputmethod.`）
   - `CFBundleExecutable`：`Babel`（必须与 `Contents/MacOS/` 下可执行文件同名）
   - `CFBundlePackageType`：`APPL`
   - `LSBackgroundOnly`：`<true/>`（输入法是无 Dock 图标、由 WindowServer 按需唤醒的后台服务进程，必须设为 `true`）
2. **IMKit 连接与控制器类**：
   - `InputMethodConnectionName`：`com.babel.inputmethod.Babel_Connection`（需与代码中 `IMKServer(name:bundleIdentifier:)` 传入的名称严格一致）
   - `InputMethodServerControllerClass`：`BabelInputController`
   - `InputMethodServerDelegateClass`：`BabelInputController`
   - **Swift 避坑要求**：Swift 类默认带有模块名前缀（如 `Babel.BabelInputController`），因此控制器类定义上方**必须添加 `@objc(BabelInputController)`**（见 [BabelInputController.swift](file:///Users/Larry/Projects/pnnh/babel/macos/BabelInputMethod/BabelInputController.swift#L4-L5)），确保 `NSClassFromString("BabelInputController")` 能正确实例化。
3. **字符集与图标键**：
   - `tsInputMethodCharacterRepertoireKey`：`["Latn", "Hans"]`
   - `tsInputMethodIconFileKey`：`icon.pdf`
4. **输入模式字典 `ComponentInputModeDict`**：
   - `tsInputModeListKey`：以模式 ID（`com.babel.inputmethod.Babel.pinyin`）为 Key 的字典，内部需显式包含：
     - `TISInputSourceID`：`com.babel.inputmethod.Babel.pinyin`
     - `tsInputModeDefaultStateKey`：`<true/>`
     - `tsInputModeIsVisibleKey`：`<true/>`
     - `tsInputModePrimaryInScriptKey`：`<true/>`
     - `tsInputModeScriptKey`：`smSimpChinese`
     - `tsInputModeMenuIconFileKey` / `tsInputModePaletteIconFileKey` / `tsInputModeAlternateMenuIconFileKey`：`icon.pdf`
   - `tsVisibleInputModeOrderedArrayKey`：包含 `com.babel.inputmethod.Babel.pinyin` 的有序数组。

### 3.3 本地化名称映射 (`InfoPlist.strings`)

`AddInputMethodAppInfoForCache` 与 `InputModeListApplier` 会根据输入法模式 ID 读取本地化显示名。必须在 [InfoPlist.strings](file:///Users/Larry/Projects/pnnh/babel/macos/BabelInputMethod/Resources/InfoPlist.strings)、[zh-Hans.lproj/InfoPlist.strings](file:///Users/Larry/Projects/pnnh/babel/macos/BabelInputMethod/Resources/zh-Hans.lproj/InfoPlist.strings) 及 [en.lproj/InfoPlist.strings](file:///Users/Larry/Projects/pnnh/babel/macos/BabelInputMethod/Resources/en.lproj/InfoPlist.strings) 中同时包含 Bundle 名称与完整的模式 ID 映射：

```text
"CFBundleDisplayName" = "Babel";
"CFBundleName" = "Babel";
"com.babel.inputmethod.Babel.pinyin" = "Babel 拼音";
```

注意：**必须保留根目录 `Resources/InfoPlist.strings` 兜底文件**，否则在系统语言与 `.lproj` 不完全匹配时，系统可能读不到 `LocalizedName` 而导致输入源标题为空或被界面过滤。

### 3.4 `IMKServer` 初始化生命周期

在 [main.swift](file:///Users/Larry/Projects/pnnh/babel/macos/BabelInputMethod/main.swift#L8-L21) 中，`IMKServer` **必须在 `NSApplication.shared.run()` 之前初始化**，并由静态属性强引用持有，绝不可延迟到 `applicationDidFinishLaunching` 之后才创建：

```swift
static var server: IMKServer?

static func main() {
    let app = NSApplication.shared
    let delegate = AppDelegate()
    app.delegate = delegate

    let connectionName =
        Bundle.main.infoDictionary?["InputMethodConnectionName"] as? String
        ?? "com.babel.inputmethod.Babel_Connection"
    let bundleID = Bundle.main.bundleIdentifier ?? "com.babel.inputmethod.Babel"

    // 必须在 app.run() 进入主 RunLoop 之前向系统注册 Mach Connection
    server = IMKServer(name: connectionName, bundleIdentifier: bundleID)

    app.run()
}
```

---

## 4. 免注销动态注册、启用与激活标准流程

### 4.1 为什么商业输入法安装后无需注销即可使用？

早期误以为只有“注销重新登录”才能让系统扫描新输入法；实际上商业输入法的安装包只是在拷贝完 `.app` 后做了三件事：
1. 其 `CFBundleIdentifier` 符合 `".inputmethod."` 规则；
2. 在**当前用户图形会话**中调用 `TISRegisterInputSource` 触发 `islcRebuildInputSourceCache`；
3. 当场枚举 `TISCreateInputSourceList(nil, true)` 找到目标输入源并调用 `TISEnableInputSource` + `TISSelectInputSource`。

### 4.2 Babel 的 5 步自动化安装与激活流水线

由 [build_and_install_imk.sh](file:///Users/Larry/Projects/pnnh/babel/macos/build_and_install_imk.sh) 与 [tis_register.swift](file:///Users/Larry/Projects/pnnh/babel/macos/tis_register.swift) 协作完成：

1. **清理扩展属性与代码签名**：
   - 使用 `codesign --force --deep --sign -` 对 `Babel.app` 执行 Ad-hoc 签名；
   - 拷贝到 `/Library/Input Methods/Babel.app` 后清理 `com.apple.quarantine` 等扩展属性（`xattr -dr` / `xattr -cr`），权限设为 `755`。
2. **刷新 LaunchServices 数据库**：
   - 以实际登录用户身份执行 `lsregister -f "/Library/Input Methods/Babel.app"`，确保系统立即感知最新 `Info.plist` 元数据。
3. **触发 TIS 缓存重建**：
   - 调用 `TISRegisterInputSource(url as CFURL)` 触发 `HIToolbox` 扫描目录。
4. **检索全部已安装输入源（含未启用项）**：
   - 调用 `TISCreateInputSourceList(nil, true)`（注意：**第二个参数 `includeAllInstalled` 必须为 `true`**）。
5. **两阶段启用与选中 + 刷新菜单栏**：
   - 先对检索到的所有匹配输入源（包括父级 `TISTypeKeyboardInputMethodBundle` `com.babel.inputmethod.Babel` 与子级 `TISTypeKeyboardInputMode` `com.babel.inputmethod.Babel.pinyin`）调用 `TISEnableInputSource`；
   - 再对 `kTISPropertyInputSourceIsSelectCapable == true` 的子模式调用 `TISSelectInputSource`；
   - 通过 `open "/Library/Input Methods/Babel.app"` 触发一次会话注册，并重启 `TextInputMenuAgent`、`TextInputSwitcher` 与 `System Settings` 清理进程缓存。

---

## 5. 本次会话踩坑记录与排错指南（Pitfalls & Troubleshooting）

| 编号 | 现象 / 报错 | 根本原因 | 解决方案 |
| :--- | :--- | :--- | :--- |
| **1** | `TISRegisterInputSource` 返回 `0`，注销重登后「系统设置」和 TIS 列表仍找不到 Babel | `CFBundleIdentifier` 为 `com.babel.inputmethod`（3 段），不包含 `".inputmethod."`（两边带点），被 `HIToolbox` 的 `TISFileInterrogator` 静默跳过 | 改为 4 段式 `com.babel.inputmethod.Babel`，并同步更新模式 ID 与 `InfoPlist.strings` |
| **2** | 仅根据 `TISRegisterInputSource` 返回 `0` 误判注册成功 | `TISRegisterInputSource` 在现代 macOS 汇编实现中直接触发缓存重建并无条件 `mov w0, #0; ret` | 必须紧接着调用 `TISCreateInputSourceList(nil, true)` 二次查验是否真正进入 TIS 列表 |
| **3** | `TISCreateInputSourceList(nil, false)` 查询不到刚安装的输入法 | 第二参数 `includeAllInstalled: false` 仅返回**已启用**输入源，刚安装但未启用的输入法不会返回 | 查验与激活脚本中统一传 `includeAllInstalled: true` |
| **4** | `sudo` 脚本里通过 `sudo -u $USER swift -` 执行注册报 `/tmp/clang-module-cache/...: Permission denied` | `root` 此前在共享的 `/tmp/clang-module-cache` 创建了模块缓存目录，普通用户无写入权限；且 `sudo` 编译产物会变成 `root` 所有导致后续非 `sudo` 构建 `rm -rf` 失败 | 将注册工具预编译为原生二进制 `build/macos/bin/tis_register`；指定独立 `-module-cache-path build/macos/.cache`；`sudo` 构建结束时用 `chown -R $SUDO_USER` 归还权限 |
| **5** | `sudo ./macos/build_and_install_imk.sh --install-system` 下 `sudo -u $USER` 偶发无法在当前桌面 GUI 会话同步 TIS | `sudo` 子进程脱离当前登录用户的 Aqua/WindowServer 引导域（Bootstrap Namespace） | 如脚本内未当场识别，直接在当前登录用户的**普通终端**执行 `./build/macos/bin/tis_register "/Library/Input Methods/Babel.app"` |
| **6** | CMake 构建后 `./build/macos/bin/Babel` 不存在 | macOS 下 `qt_add_executable(Babel MACOSX_BUNDLE ...)` 使用 Xcode 生成器会将主程序输出到 `build/macos/babel/Debug/Babel.app/Contents/MacOS/Babel` | 在 [babel/CMakeLists.txt](file:///Users/Larry/Projects/pnnh/babel/babel/CMakeLists.txt#L108-L117) 添加 `POST_BUILD` 命令自动创建到 `build/macos/bin/Babel` 的软链接 |
| **7** | 后端 `UnixDomainSocketServer` 编译期报错缺符号或类型不匹配 | C++ UDS 服务端使用了 `QLocalServer` 和 `std::string` 路径 | 在 [babel/CMakeLists.txt](file:///Users/Larry/Projects/pnnh/babel/babel/CMakeLists.txt#L14) 与 [babel/src/CMakeLists.txt](file:///Users/Larry/Projects/pnnh/babel/babel/src/CMakeLists.txt#L35-L36) 引入 `Qt6::Network`；在 [UnixDomainSocketServer.cpp](file:///Users/Larry/Projects/pnnh/babel/babel/src/ipc/UnixDomainSocketServer.cpp#L55) 使用 `QString::fromStdString` |

---

## 6. 高阶诊断与日常调试命令速查

### 6.1 开启 `HIToolbox` 内置 TIS 缓存重建调试日志

通过反汇编发现 `HIToolbox` 内置了偏好开关 `AppleTISTraceCacheRebuild`（由 `TBGetBooleanPref` 读取）。当系统再次拒绝加载某个输入法时，可直接打开它观察 `TISFileInterrogator` 与 `AddInputMethodAppInfoForCache` 的详细日志：

```bash
# 1. 开启 HIToolbox TIS 缓存重建追踪日志
defaults write -g AppleTISTraceCacheRebuild -bool TRUE

# 2. 实时抓取系统级命中日志（另一终端运行）
log stream --predicate 'process == "tis_register" OR process == "TextInputMenuAgent" OR subsystem CONTAINS[c] "HIToolbox"' --info --debug

# 3. 触发缓存重建并注册
./build/macos/bin/tis_register "/Library/Input Methods/Babel.app"

# 4. 排查完毕后关闭调试开关
defaults delete -g AppleTISTraceCacheRebuild
```

### 6.2 查看 Babel 前后端实时运行日志

- **后端守护进程启动与日志**：
  - 前台启动（推荐开发调试）：`./build/macos/bin/Babel --daemon`
  - 日志文件轮转路径：`~/.local/state/babel/daemon.log`（见 [DaemonLogger.cpp](file:///Users/Larry/Projects/pnnh/babel/babel/src/logging/DaemonLogger.cpp#L22)）
  - UDS 套接字路径：`~/.local/run/babel.sock`（见 [UnixDomainSocketServer.cpp](file:///Users/Larry/Projects/pnnh/babel/babel/src/ipc/UnixDomainSocketServer.cpp#L40) 与 [IpcClient.swift](file:///Users/Larry/Projects/pnnh/babel/macos/BabelInputMethod/IpcClient.swift#L15)）
- **前端 IMKit 实时日志**（`os.Logger` subsystem 为 `com.babel.inputmethod`，见 [Logger.swift](file:///Users/Larry/Projects/pnnh/babel/macos/BabelInputMethod/Logger.swift#L6)）：

```bash
log stream --predicate 'subsystem == "com.babel.inputmethod"' --level debug
```

### 6.3 修改 Swift 前端代码后的免注销热重装命令

修改 [BabelInputController.swift](file:///Users/Larry/Projects/pnnh/babel/macos/BabelInputMethod/BabelInputController.swift) 等前端代码后，无需注销，只需覆盖安装并杀掉旧的 `Babel` 输入法进程（切换输入法时 WindowServer 会自动重新拉起新版二进制）：

```bash
sudo ./macos/build_and_install_imk.sh --install-system
killall -9 Babel 2>/dev/null || true
```

