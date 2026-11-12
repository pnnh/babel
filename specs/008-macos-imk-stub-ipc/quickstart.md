# Quickstart: macOS 原生输入法接入原型与 IPC 骨架

**Feature**: `008-macos-imk-stub-ipc` | **Date**: 2026-11-12

---

## 1. 环境准备

* **操作系统**: macOS 14.0+ (Apple Silicon 或 Intel)
* **编译器**: Xcode 16+ (包含 Swift 6.0+)、Clang/Clang++
* **构建工具**: CMake 3.29+
* **依赖库**: Qt 6.x (开发机已配置)、spdlog、unofficial-sqlite3

---

## 2. 编译与构建

在项目根目录下通过 CMake 预设或常规命令构建：

```bash
# 配置工程
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# 编译全部目标（包含 babel 可执行程序、Swift 输入法 Bundle 与测试）
cmake --build build -j8
```

---

## 3. 运行验证场景

### 场景 1：开发期 Qt 独立浮窗模式验证（回归保障）
无需安装系统输入法，直接在终端启动：
```bash
./build/bin/babel --standalone
```
* **验证**: 按下 `Control+Option+Space`，原 QML 浮窗正常唤起，打字与预览正常工作。

### 场景 2：后端守护进程启动
```bash
./build/bin/babel --daemon
```
* **观察日志**: 检查控制台输出与 `$HOME/.local/state/babel/daemon.log`，确认 Unix Domain Socket 成功在 `$HOME/.local/run/babel.sock` 监听，且模型路径成功解析至 `$HOME/.local/models/`。

### 场景 3：安装并激活系统输入法
```bash
# 将构建出的输入法 Bundle 拷贝至个人输入法目录
cp -R build/bin/BabelInputMethod.app ~/Library/Input\ Methods/

# 刷新系统输入法缓存并重启输入源服务
killall -9 TextInputMenuAgent || true
```
1. 打开 macOS **系统设置 -> 键盘 -> 文本输入 -> 输入源**；
2. 点击添加（+），在“简体中文”分类下选择 **Babel** 并添加；
3. 打开 **TextEdit**（备忘录或文本编辑器），切换输入法为 Babel；
4. 键入测试按键，观察字符通过 `insertText` 原生直接上屏。

### 场景 4：实时日志追踪
打开独立终端窗口运行：
```bash
# 追踪 Swift 前端日志
log stream --predicate 'subsystem == "com.babel.inputmethod"' --level debug

# 追踪 C++ 守护进程日志
tail -f ~/.local/state/babel/daemon.log
```

### 场景 5：自动化单元测试
```bash
ctest --test-dir build --output-on-failure
```
确保模型路径解析（`ModelPathsTest`）与 IPC 基础序列化单元测试全部通过。

