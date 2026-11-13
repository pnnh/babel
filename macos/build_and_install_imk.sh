#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="${PROJECT_ROOT}/build/macos/bin"
CACHE_DIR="${PROJECT_ROOT}/build/macos/.cache"
APP_NAME="Babel.app"
APP_BUNDLE="${OUTPUT_DIR}/${APP_NAME}"
CONTENTS_DIR="${APP_BUNDLE}/Contents"
MACOS_DIR="${CONTENTS_DIR}/MacOS"
RESOURCES_DIR="${CONTENTS_DIR}/Resources"

# 每次构建前先清理旧临时 Bundle，防止因权限残留导致覆盖失败
rm -rf "$APP_BUNDLE"
mkdir -p "$MACOS_DIR" "$RESOURCES_DIR" "$CACHE_DIR"

echo "==> 确保 icon.pdf 图标资源已就绪..."
if [[ ! -f "${SCRIPT_DIR}/BabelInputMethod/Resources/icon.pdf" ]]; then
    swift -module-cache-path "$CACHE_DIR" - << 'EOF'
import Cocoa
let size = CGSize(width: 16, height: 16)
let pdfData = NSMutableData()
guard let consumer = CGDataConsumer(data: pdfData as CFMutableData),
      let context = CGContext(consumer: consumer, mediaBox: &CGRect(origin: .zero, size: size), nil) else { exit(1) }
var box = CGRect(origin: .zero, size: size)
context.beginPage(mediaBox: &box)
let path = CGPath(roundedRect: CGRect(x: 1, y: 1, width: 14, height: 14), cornerWidth: 3, cornerHeight: 3, transform: nil)
context.setFillColor(CGColor(red: 0.15, green: 0.15, blue: 0.15, alpha: 1.0))
context.addPath(path)
context.fillPath()
let font = CTFontCreateWithName("Helvetica-Bold" as CFString, 10, nil)
let attr: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: NSColor.white]
let line = CTLineCreateWithAttributedString(NSAttributedString(string: "B", attributes: attr))
context.textPosition = CGPoint(x: 4.5, y: 4)
CTLineDraw(line, context)
context.endPage()
context.closePDF()
pdfData.write(toFile: "macos/BabelInputMethod/Resources/icon.pdf", atomically: true)
EOF
fi

echo "==> 编译 Babel 前端输入法 (Swift 6)..."
swiftc \
    -O \
    -parse-as-library \
    -module-cache-path "$CACHE_DIR" \
    -framework Cocoa \
    -framework InputMethodKit \
    -o "${MACOS_DIR}/Babel" \
    "${SCRIPT_DIR}/BabelInputMethod/"*.swift

echo "==> 编译 TIS 注册与激活辅助工具 (tis_register)..."
swiftc \
    -O \
    -module-cache-path "$CACHE_DIR" \
    -framework Foundation \
    -framework Carbon \
    -o "${OUTPUT_DIR}/tis_register" \
    "${SCRIPT_DIR}/tis_register.swift"

echo "==> 拷贝 Info.plist 与资源..."
cp "${SCRIPT_DIR}/BabelInputMethod/Info.plist" "${CONTENTS_DIR}/Info.plist"
if [[ -d "${SCRIPT_DIR}/BabelInputMethod/Resources" ]]; then
    cp -R "${SCRIPT_DIR}/BabelInputMethod/Resources/"* "${RESOURCES_DIR}/"
fi

echo "==> 生成 PkgInfo..."
printf "APPL????" > "${CONTENTS_DIR}/PkgInfo"

echo "==> 执行本地 Ad-hoc 代码签名..."
codesign --force --deep --sign - "${APP_BUNDLE}"

echo "==> 构建成功: ${APP_BUNDLE}"

# 若通过 sudo 执行构建，将构建目录所有权归还给实际用户，避免工作区残留 root 权限文件
if [[ "$EUID" -eq 0 ]] && [[ -n "${SUDO_USER:-}" ]]; then
    chown -R "${SUDO_USER}" "${OUTPUT_DIR}" "${CACHE_DIR}" 2>/dev/null || true
fi

if [[ "${1:-}" == "--install" || "${1:-}" == "--install-system" ]]; then
    INSTALL_MODE="${1:-}"
    REAL_USER="${SUDO_USER:-$(whoami)}"

    if [[ "$INSTALL_MODE" == "--install-system" ]] || [[ "$EUID" -eq 0 ]]; then
        TARGET_DIR="/Library/Input Methods"
    else
        TARGET_DIR="${HOME}/Library/Input Methods"
    fi

    echo "==> 目标安装目录: ${TARGET_DIR}/${APP_NAME}"
    mkdir -p "$TARGET_DIR"
    rm -rf "${TARGET_DIR}/${APP_NAME}"
    cp -R "${APP_BUNDLE}" "${TARGET_DIR}/"

    # 清除潜在隔离与安全标记
    xattr -dr com.apple.quarantine "${TARGET_DIR}/${APP_NAME}" 2>/dev/null || true
    xattr -cr "${TARGET_DIR}/${APP_NAME}" 2>/dev/null || true
    chmod -R 755 "${TARGET_DIR}/${APP_NAME}"

    # 注册 LaunchServices 数据库（针对当前登录用户）
    echo "==> 刷新 LaunchServices 数据库 (用户: ${REAL_USER})..."
    if [[ "$EUID" -eq 0 ]] && [[ -n "${SUDO_USER:-}" ]]; then
        sudo -u "$REAL_USER" /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "${TARGET_DIR}/${APP_NAME}" 2>/dev/null || true
    else
        /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "${TARGET_DIR}/${APP_NAME}" 2>/dev/null || true
    fi

    # 运行编译好的独立 tis_register 二进制工具登记并激活
    echo "==> 执行 TIS 显式登记与激活 (用户: ${REAL_USER})..."
    if [[ "$EUID" -eq 0 ]] && [[ -n "${SUDO_USER:-}" ]]; then
        sudo -u "$REAL_USER" "${OUTPUT_DIR}/tis_register" "${TARGET_DIR}/${APP_NAME}"
    else
        "${OUTPUT_DIR}/tis_register" "${TARGET_DIR}/${APP_NAME}"
    fi

    # 启动一次以触发 IMKServer 注册（以登录用户身份运行）
    echo "==> 启动输入法服务触发用户会话..."
    if [[ "$EUID" -eq 0 ]] && [[ -n "${SUDO_USER:-}" ]]; then
        sudo -u "$REAL_USER" open "${TARGET_DIR}/${APP_NAME}" 2>/dev/null || true
    else
        open "${TARGET_DIR}/${APP_NAME}" 2>/dev/null || true
    fi

    # 彻底关闭任何可能持有旧缓存的设置面板与输入菜单进程
    echo "==> 刷新系统输入菜单与设置面板缓存..."
    if [[ "$EUID" -eq 0 ]] && [[ -n "${SUDO_USER:-}" ]]; then
        sudo -u "$REAL_USER" killall -9 "System Settings" "系统设置" 2>/dev/null || true
        sudo -u "$REAL_USER" killall -9 TextInputMenuAgent TextInputSwitcher 2>/dev/null || true
    else
        killall -9 "System Settings" "系统设置" 2>/dev/null || true
        killall -9 TextInputMenuAgent TextInputSwitcher 2>/dev/null || true
    fi

    echo "==> 安装完成！"
fi
