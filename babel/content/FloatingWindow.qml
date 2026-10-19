import QtQuick
import QtQuick.Window

// 无边框浮窗：原始输入槽 / 中文输出预览 / 学习提示气泡三段式布局（FR-002）。
// 数据绑定来源：babel/src/session/SessionController（contracts/qml-cpp-session-interface.md），
// 以 "SessionController" 名称注册为 QML 上下文属性（见 babel/main.cpp）。
Window {
    id: floatingWindow
    objectName: "floatingWindow"

    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
    width: 480
    height: 270
    visible: false
    color: "transparent"

    Connections {
        target: SessionController
        function onSessionInvoked() {
            floatingWindow.visible = true;
            rawInput.clear();
            rawInput.forceActiveFocus();
        }
        function onSessionClosed(reason) {
            floatingWindow.visible = false;
        }
    }

    Rectangle {
        id: mainView
        anchors.fill: parent
        color: "#2b2b2b"
        border.color: "#555555"
        radius: 8
        visible: !SessionController.settingsVisible

        // 浮窗右上角设置入口（FR-010）：点击进入设置页面，复用同一 Window 内的
        // 视图切换方案（research.md §11），不新建独立 Window。
        Rectangle {
            id: settingsButton
            width: 28
            height: 28
            radius: 14
            color: "#444444"
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 8
            z: 1

            Text {
                anchors.centerIn: parent
                text: "⚙"
                color: "white"
                font.pixelSize: 16
            }

            MouseArea {
                anchors.fill: parent
                onClicked: SessionController.openSettings()
            }
        }

        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            // 原始输入槽：承载用户实际击键捕获（本地可编辑缓冲区，不与外部数据绑定，
            // 避免每次击键后被 SessionController 推送的规整化候选文本覆盖、吞掉正在
            // 输入中的空格/未完成词——`rawInputSlotText` 按契约展示的是规整化候选，
            // 不是逐字回显，见下方单独的规整化预览行）。
            Rectangle {
                width: parent.width
                height: 64
                color: "#1e1e1e"
                radius: 4

                TextInput {
                    id: rawInput
                    anchors.fill: parent
                    anchors.margins: 8
                    color: "white"
                    font.pixelSize: 16
                    focus: true
                    Keys.onReturnPressed: SessionController.confirm()
                    Keys.onEnterPressed: SessionController.confirm()
                    Keys.onEscapePressed: SessionController.cancel()
                    onTextEdited: SessionController.submitKeystrokes(text)
                }
            }

            // 规整化候选预览（contracts/qml-cpp-session-interface.md："展示
            // NormalizedCandidateText.content，不是用户原始击键的逐字回显"）。
            Text {
                width: parent.width
                visible: SessionController.rawInputSlotText.length > 0
                color: "#8ab4f8"
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                text: SessionController.rawInputSlotText
            }


            // 中文输出预览区域：标准中文译写结果 / 等待状态提示 / 模型不可用提示。
            Rectangle {
                width: parent.width
                height: 90
                color: "#1e1e1e"
                radius: 4

                Text {
                    id: outputPreviewText
                    anchors.fill: parent
                    anchors.margins: 8
                    wrapMode: Text.WordWrap
                    font.pixelSize: 16
                    color: SessionController.modelAvailable ? "white" : "#888888"
                    text: {
                        // 模型不可用（FR-011）：浮窗仍可操作，明确提示生成功能不可用。
                        if (!SessionController.modelAvailable) {
                            return "标准中文生成功能不可用（本地模型文件缺失/损坏/无法加载）";
                        }
                        // Debouncing/Generating：展示可感知的等待状态纯文本提示
                        // （User Story 3 验收场景 2，不要求动画效果）。
                        if (SessionController.outputPreviewState === "Debouncing"
                                || SessionController.outputPreviewState === "Generating") {
                            return SessionController.outputPreviewText.length > 0
                                    ? SessionController.outputPreviewText + "\n（生成中…）"
                                    : "生成中…";
                        }
                        return SessionController.outputPreviewText;
                    }
                }
            }

            // 学习提示气泡（可选，低优先级，FR-002）：为空/不可见时不渲染。
            Text {
                width: parent.width
                visible: SessionController.learningTipVisible
                color: "#aaaaaa"
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                text: SessionController.learningTipText
            }

            // 手动粘贴提示（FR-009/FR-012，与 outcome 具体取值无关的统一文案）。
            Text {
                width: parent.width
                visible: SessionController.manualPasteHintVisible
                color: "#ffcc66"
                font.pixelSize: 14
                wrapMode: Text.WordWrap
                text: "已复制到剪贴板，请手动粘贴"
            }
        }
    }

    // 设置视图（同一 Window 内视图切换，research.md §11）：settingsVisible=true
    // 时展示，默认隐藏；与 mainView 互斥显示，不新建独立 Window。
    SettingsPanel {
        anchors.fill: parent
        visible: SessionController.settingsVisible
    }
}
