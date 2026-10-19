import QtQuick

// 设置页面（FR-010/FR-011/FR-012/FR-013/FR-014）：展示约定本地模型目录下扫描到
// 的全部模型文件、三档推荐信息，支持点选热切换模型；复用同一 FloatingWindow 内
// 视图切换方案（research.md §11），非独立 Window。
// 数据绑定来源：babel/src/session/SessionController
// （contracts/qml-cpp-session-interface.md 二/三/四）。
Rectangle {
    id: settingsPanel
    color: "#2b2b2b"
    radius: 8

    Column {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Row {
            width: parent.width
            Text {
                text: "模型设置"
                color: "white"
                font.pixelSize: 18
                font.bold: true
            }
            Item { width: parent.width - 160; height: 1 }
            Rectangle {
                width: 56
                height: 28
                radius: 4
                color: "#444444"
                Text {
                    anchors.centerIn: parent
                    text: "返回"
                    color: "white"
                    font.pixelSize: 13
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: SessionController.closeSettings()
                }
            }
        }

        Text {
            width: parent.width
            visible: SessionController.modelSwitching
            text: "模型加载中，请稍候…"
            color: "#ffcc66"
            font.pixelSize: 14
        }

        Text {
            width: parent.width
            visible: !SessionController.modelSwitching && SessionController.fallbackNoticeText.length > 0
            text: SessionController.fallbackNoticeText
            color: "#ffcc66"
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            text: "当前模型：" + (SessionController.currentModelName.length > 0
                                   ? SessionController.currentModelName : "（不可用）")
            color: "#8ab4f8"
            font.pixelSize: 13
        }

        Text {
            text: "推荐档位（供参考，不限制实际可选范围）"
            color: "#aaaaaa"
            font.pixelSize: 13
        }

        Repeater {
            model: SessionController.recommendedProfiles
            delegate: Text {
                width: settingsPanel.width - 24
                wrapMode: Text.WordWrap
                color: "#aaaaaa"
                font.pixelSize: 12
                text: "· " + modelData.tier + "档：" + modelData.modelName + "（" + modelData.quantization
                      + "，" + modelData.approxSizeDescription + "）目标硬件：" + modelData.targetHardwareDescription
            }
        }

        Text {
            text: "本地模型目录文件（点选切换，耗时操作，热切换期间慢流暂不可用）"
            color: "#aaaaaa"
            font.pixelSize: 13
        }

        Column {
            width: parent.width
            spacing: 4

            Repeater {
                model: SessionController.availableModels
                delegate: Rectangle {
                    width: settingsPanel.width - 24
                    height: 36
                    radius: 4
                    color: modelData.fileName === SessionController.currentModelName
                           ? "#3a3a3a" : "#1e1e1e"
                    opacity: modelData.availability === "KnownUnloadable" ? 0.55 : 1.0

                    Row {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 6
                        Text {
                            text: modelData.fileName
                            color: "white"
                            font.pixelSize: 13
                        }
                        Text {
                            visible: modelData.availability === "KnownUnloadable"
                            text: "⚠ 不可用"
                            color: "#ff8866"
                            font.pixelSize: 12
                        }
                        Text {
                            visible: modelData.tierHint !== "Unknown"
                            text: "[" + modelData.tierHint + "]"
                            color: "#8ab4f8"
                            font.pixelSize: 12
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: !SessionController.modelSwitching
                        onClicked: SessionController.switchModel(modelData.filePath)
                    }
                }
            }

            Text {
                visible: SessionController.availableModels.length === 0
                text: "（约定本地模型目录下未发现任何 .gguf 文件）"
                color: "#888888"
                font.pixelSize: 12
            }
        }

        // T034（GPLv3 copyleft 履约，research.md §3 步骤 3）：随包分发的拼音词库
        // 数据来源与许可证声明，设置页面作为本应用当前最接近"关于/文档入口"的
        // 承载位置（001 尚无独立 About 页面）。
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "拼音词库数据来源于 iDvel/rime-ice（GPLv3），详见随包 LICENSE-rime-ice.txt / NOTICE-rime-ice.txt"
            color: "#666666"
            font.pixelSize: 11
        }
    }
}
