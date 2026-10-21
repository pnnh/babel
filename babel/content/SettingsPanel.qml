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

        // 004-speculative-decoding（T019，FR-004/FR-007，contracts/qml-cpp-
        // session-interface-increment.md 一）：草稿模型候选规格展示，复用既有
        // "推荐档位" Repeater 模式。
        Text {
            text: "草稿模型（投机解码加速，可选）"
            color: "#aaaaaa"
            font.pixelSize: 13
        }

        Repeater {
            model: SessionController.recommendedDraftProfiles
            delegate: Text {
                width: settingsPanel.width - 24
                wrapMode: Text.WordWrap
                color: "#aaaaaa"
                font.pixelSize: 12
                text: "· " + modelData.modelName + "（" + modelData.quantization
                      + "，" + modelData.approxSizeDescription + "）"
            }
        }

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "下载以上任一候选规格即可被自动识别并搭配当前目标模型档位使用，无需手动指定搭配关系"
            color: "#888888"
            font.pixelSize: 11
        }

        // 事后修订（用户决策）：实测发现当前实现在本机反而显著变慢（详见
        // quickstart.md"实测记录"），故改为默认关闭的实验性开关，不自动启用；
        // 纯 QtQuick 实现（本文件全程未 import QtQuick.Controls），不新增依赖。
        Row {
            spacing: 8
            Rectangle {
                id: speculativeToggle
                width: 36
                height: 20
                radius: 10
                anchors.verticalCenter: parent.verticalCenter
                color: SessionController.speculativeDecodingEnabled ? "#4caf50" : "#555555"

                Rectangle {
                    width: 16
                    height: 16
                    radius: 8
                    color: "#ffffff"
                    anchors.verticalCenter: parent.verticalCenter
                    x: SessionController.speculativeDecodingEnabled ? parent.width - width - 2 : 2
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: SessionController.speculativeDecodingEnabled = !SessionController.speculativeDecodingEnabled
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "投机解码加速（实验性，默认关闭）"
                color: "#aaaaaa"
                font.pixelSize: 12
            }
        }

        // 004-speculative-decoding（T024，FR-006，contracts/qml-cpp-session-
        // interface-increment.md 一）：投机解码是否启用及原因提示，复用既有
        // fallbackNoticeText 展示样式。
        Text {
            width: parent.width
            visible: SessionController.speculativeDecodingNoticeText.length > 0
            text: SessionController.speculativeDecodingNoticeText
            color: "#ffcc66"
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        // 005-learning-tip-generation（T011，data-model.md 五，FR-008）：学习
        // 提示气泡开关，默认开启；复用上方投机解码开关完全相同的纯 Rectangle
        // （圆角滑块轨道+圆形滑块）+ MouseArea 实现模式，不引入
        // QtQuick.Controls 依赖。
        Row {
            spacing: 8
            Rectangle {
                id: learningTipToggle
                width: 36
                height: 20
                radius: 10
                anchors.verticalCenter: parent.verticalCenter
                color: SessionController.learningTipEnabled ? "#4caf50" : "#555555"

                Rectangle {
                    width: 16
                    height: 16
                    radius: 8
                    color: "#ffffff"
                    anchors.verticalCenter: parent.verticalCenter
                    x: SessionController.learningTipEnabled ? parent.width - width - 2 : 2
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: SessionController.learningTipEnabled = !SessionController.learningTipEnabled
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "学习提示气泡（默认开启）"
                color: "#aaaaaa"
                font.pixelSize: 12
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
