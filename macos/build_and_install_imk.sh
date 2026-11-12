#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="${PROJECT_ROOT}/build/macos/bin"
APP_NAME="Babel.app"
APP_BUNDLE="${OUTPUT_DIR}/${APP_NAME}"
CONTENTS_DIR="${APP_BUNDLE}/Contents"
MACOS_DIR="${CONTENTS_DIR}/MacOS"
RESOURCES_DIR="${CONTENTS_DIR}/Resources"

mkdir -p "$MACOS_DIR" "$RESOURCES_DIR"

echo "==> 编译 BabelInputMethod (Swift 6)..."
swiftc \
    -O \
    -parse-as-library \
    -module-cache-path /tmp/clang-module-cache \
    -framework Cocoa \
    -framework InputMethodKit \
    -o "${MACOS_DIR}/BabelInputMethod" \
    "${SCRIPT_DIR}/BabelInputMethod/"*.swift

echo "==> 拷贝 Info.plist 与本地化资源 (Resources)..."
cp "${SCRIPT_DIR}/BabelInputMethod/Info.plist" "${CONTENTS_DIR}/Info.plist"
if [[ -d "${SCRIPT_DIR}/BabelInputMethod/Resources" ]]; then
    cp -R "${SCRIPT_DIR}/BabelInputMethod/Resources/"* "${RESOURCES_DIR}/"
fi

echo "==> 执行本地 Ad-hoc 代码签名..."
codesign --force --deep --sign - "${APP_BUNDLE}"

echo "==> 构建成功: ${APP_BUNDLE}"

if [[ "${1:-}" == "--install" ]]; then
    TARGET_DIR="${HOME}/Library/Input Methods"
    mkdir -p "$TARGET_DIR"
    echo "==> 安装至 ${TARGET_DIR}/${APP_NAME}..."
    rm -rf "${TARGET_DIR}/${APP_NAME}"
    cp -R "${APP_BUNDLE}" "${TARGET_DIR}/"

    # 清除潜在隔离属性
    xattr -cr "${TARGET_DIR}/${APP_NAME}" 2>/dev/null || true

    # 注册 LaunchServices 数据库
    echo "==> 刷新 LaunchServices 数据库..."
    /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "${TARGET_DIR}/${APP_NAME}" 2>/dev/null || true

    # 启动一次以触发 IMKServer 注册
    echo "==> 启动输入法服务触发 IMKServer 系统注册..."
    open "${TARGET_DIR}/${APP_NAME}" 2>/dev/null || true

    # 重启输入法相关系统服务
    echo "==> 重启 TextInputMenuAgent / TextInputSwitcher..."
    killall -9 TextInputMenuAgent TextInputSwitcher 2>/dev/null || true

    echo "==> 安装就绪！请前往「系统设置 -> 键盘 -> 文本输入 -> 输入源」添加 Babel。"
fi
