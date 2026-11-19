// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    readonly property var i18n: backend.i18n
    readonly property color bgColor: "#141821"
    readonly property color cardColor: "#1E2430"
    readonly property color cardBorder: "#2D3648"
    readonly property color subCardColor: "#171C26"
    readonly property color textPrimary: "#F1F5F9"
    readonly property color textSecondary: "#94A3B8"
    readonly property color accentBlue: "#38BDF8"
    readonly property color pillBg: "#273549"

    title: (backend.connected && backend.deviceName) ? (backend.deviceName + " — Kalm") : "Kalm"
    width: 640
    height: 740
    minimumWidth: 500
    minimumHeight: 540
    color: root.bgColor

    onClosing: function(close) {
        if (!backend.quitting) {
            close.accepted = false
            root.visible = false
        }
    }

    Connections {
        target: backend
        function onWindowShowRequested() {
            root.visible = true
            root.showNormal()
            root.raise()
            root.requestActivate()
        }
    }

    // ---- Top Header Bar ----
    header: Rectangle {
        height: 52
        color: "#191F2B"
        border.color: root.cardBorder
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 8

            ToolButton {
                text: "☰"
                font.pixelSize: 18
                ToolTip.visible: hovered
                ToolTip.text: drawer.opened ? root.i18n.drawer_close : root.i18n.drawer_open
                onClicked: drawer.opened ? drawer.close() : drawer.open()
            }

            Label {
                text: "Kalm"
                font.pixelSize: 17
                font.weight: Font.DemiBold
                color: root.textPrimary
                Layout.fillWidth: true
            }

            Button {
                text: "+ " + root.i18n.action_add_earbuds
                onClicked: {
                    backend.scanBluetoothDevices()
                    addDeviceDialog.open()
                }
            }

            Button {
                visible: backend.connected
                text: root.i18n.action_refresh
                onClicked: backend.refreshNow()
            }
        }
    }

    // ---- Side Drawer (Language + Actions) ----
    Drawer {
        id: drawer
        width: 280
        height: root.height
        modal: true

        background: Rectangle {
            color: root.cardColor
            border.color: root.cardBorder
            border.width: 1
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            Label {
                text: "Kalm"
                font.pixelSize: 18
                font.weight: Font.Bold
                color: root.textPrimary
            }

            Rectangle { height: 1; Layout.fillWidth: true; color: root.cardBorder }

            Label {
                text: root.i18n.lang_title
                font.weight: Font.DemiBold
                color: root.textSecondary
                font.pixelSize: 12
            }

            ComboBox {
                id: drawerLangCombo
                Layout.fillWidth: true
                model: backend.languageModel
                textRole: "text"
                valueRole: "value"
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: drawerLangCombo.width
                    text: modelData.text
                    highlighted: drawerLangCombo.highlightedIndex === index
                }
                currentIndex: backend.languageIndex
                displayText: (backend.languageIndex >= 0 && backend.languageIndex < backend.languageModel.length)
                             ? backend.languageModel[backend.languageIndex].text : ""
                onModelChanged: Qt.callLater(function() { drawerLangCombo.currentIndex = backend.languageIndex })
                onActivated: function(index) { backend.setLanguageByIndex(index) }
                Connections {
                    target: backend
                    function onLanguageChanged() {
                        Qt.callLater(function() { drawerLangCombo.currentIndex = backend.languageIndex })
                    }
                }
            }

            Rectangle { height: 1; Layout.fillWidth: true; color: root.cardBorder; Layout.topMargin: 4 }

            Button {
                Layout.fillWidth: true
                text: root.i18n.action_add_earbuds_ellipsis
                onClicked: {
                    drawer.close()
                    backend.scanBluetoothDevices()
                    addDeviceDialog.open()
                }
            }

            Button {
                Layout.fillWidth: true
                text: root.i18n.action_kde_bt_settings
                onClicked: {
                    drawer.close()
                    backend.openSystemBluetoothSettings()
                }
            }

            Button {
                visible: backend.supportsProbe
                Layout.fillWidth: true
                text: root.i18n.action_probe
                onClicked: {
                    drawer.close()
                    backend.runDiagnosticProbe()
                    probeDialog.open()
                }
            }

            Item { Layout.fillHeight: true }

            Rectangle { height: 1; Layout.fillWidth: true; color: root.cardBorder }

            Button {
                Layout.fillWidth: true
                text: root.i18n.action_minimize_tray
                onClicked: {
                    drawer.close()
                    root.visible = false
                }
            }

            Button {
                Layout.fillWidth: true
                text: root.i18n.action_quit
                onClicked: backend.quitApp()
            }
        }
    }

    // ---- Main Scrollable Content (No right scrollbar) ----
    Flickable {
        id: mainFlick
        anchors.fill: parent
        contentWidth: width
        contentHeight: mainCol.implicitHeight + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: mainCol
            width: Math.min(mainFlick.width - 32, 580)
            anchors.horizontalCenter: parent.horizontalCenter
            y: 16
            spacing: 0

            // Inline Banner
            Rectangle {
                id: winBanner
                visible: backend.bannerText.length > 0
                Layout.fillWidth: true
                Layout.bottomMargin: 12
                implicitHeight: bannerRow.implicitHeight + 20
                radius: 10
                color: backend.bannerType === 3 ? "#3B1C22" : (backend.bannerType === 1 ? "#163229" : "#1E2D3D")
                border.color: backend.bannerType === 3 ? "#EF4444" : (backend.bannerType === 1 ? "#10B981" : "#38BDF8")
                border.width: 1

                RowLayout {
                    id: bannerRow
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Label {
                        text: backend.bannerText
                        color: root.textPrimary
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    ToolButton {
                        text: "✕"
                        onClicked: backend.clearBanner()
                    }
                }
            }

            // ---- Centered Empty State When Disconnected ----
            Item {
                visible: !backend.connected
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(
                    300,
                    mainFlick.height - 16
                    - (winBanner.visible ? (winBanner.height + 12) : 0)
                    - winFooter.implicitHeight - 28
                )

                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 32, 380)
                    spacing: 8

                    Label {
                        text: "🎧"
                        font.pixelSize: 36
                        Layout.alignment: Qt.AlignHCenter
                        opacity: 0.45
                    }

                    Label {
                        text: root.i18n.empty_connect_title
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        color: root.textPrimary
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Label {
                        text: root.i18n.empty_connect_desc
                        font.pixelSize: 13
                        color: root.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Button {
                        visible: backend.deviceMac.length > 0
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 6
                        text: root.i18n.action_connect_bt + " (" + backend.deviceName + ")"
                        onClicked: backend.connectBluetoothDevice(backend.deviceMac)
                    }
                }
            }

            // ---- 1. Centered Connection Header Card ----
            Rectangle {
                visible: backend.connected
                Layout.fillWidth: true
                implicitHeight: hdrCol.implicitHeight + 28
                radius: 12
                color: root.cardColor
                border.color: root.cardBorder
                border.width: 1

                ColumnLayout {
                    id: hdrCol
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 4

                    Label {
                        text: "🎧"
                        font.pixelSize: 28
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Label {
                        text: backend.deviceName || root.i18n.status_no_device_selected
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        color: root.textPrimary
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Label {
                        text: backend.statusSummary
                        font.pixelSize: 13
                        color: root.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            // ---- 2. Battery Grid (Left / Right / Case) ----
            RowLayout {
                visible: backend.connected
                Layout.fillWidth: true
                Layout.topMargin: 10
                spacing: 10

                Repeater {
                    model: [
                        { val: backend.leftBatteryText, sub: backend.leftBatterySub },
                        { val: backend.rightBatteryText, sub: backend.rightBatterySub },
                        { val: backend.caseBatteryText, sub: backend.caseBatterySub }
                    ]

                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: bCol.implicitHeight + 22
                        radius: 12
                        color: root.cardColor
                        border.color: root.cardBorder
                        border.width: 1

                        ColumnLayout {
                            id: bCol
                            anchors.fill: parent
                            anchors.margins: 11
                            spacing: 2

                            Label {
                                text: modelData.val
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                                color: root.textPrimary
                                horizontalAlignment: Text.AlignHCenter
                                Layout.fillWidth: true
                            }

                            Label {
                                text: modelData.sub
                                font.pixelSize: 12
                                color: root.textSecondary
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }

            // ---- 3. Noise Control Section ----
            Label {
                visible: backend.connected
                text: root.i18n.section_noise
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: root.textPrimary
                Layout.topMargin: 12
                Layout.bottomMargin: 10
                Layout.leftMargin: 4
            }

            Rectangle {
                visible: backend.connected
                Layout.fillWidth: true
                implicitHeight: noiseCol.implicitHeight + 24
                radius: 12
                color: root.cardColor
                border.color: root.cardBorder
                border.width: 1
                opacity: backend.connected ? 1.0 : 0.55
                enabled: backend.connected

                ColumnLayout {
                    id: noiseCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Item {
                        id: selectorTrack
                        Layout.fillWidth: true
                        implicitHeight: 54

                        readonly property real gap: 6
                        readonly property real cellW: Math.max(0, (width - gap * 2) / 3)
                        readonly property var modeItems: [
                            { label: root.i18n.mode_anc, sym: "🔇", modeIdx: 0 },
                            { label: root.i18n.mode_off, sym: "🎧", modeIdx: 1 },
                            { label: root.i18n.mode_trans, sym: "🎙", modeIdx: 2 }
                        ]

                        Rectangle {
                            visible: backend.noiseModeIndex >= 0 && backend.noiseModeIndex <= 2
                            x: Math.max(0, backend.noiseModeIndex) * (selectorTrack.cellW + selectorTrack.gap)
                            y: 0
                            width: selectorTrack.cellW
                            height: selectorTrack.height
                            radius: 9
                            color: root.pillBg
                            border.color: root.accentBlue
                            border.width: 1

                            Behavior on x {
                                NumberAnimation { duration: 180; easing.type: Easing.InOutCubic }
                            }
                        }

                        Repeater {
                            model: selectorTrack.modeItems

                            delegate: Item {
                                id: segCell
                                required property var modelData
                                required property int index
                                readonly property bool isSelected: backend.noiseModeIndex === modelData.modeIdx

                                x: index * (selectorTrack.cellW + selectorTrack.gap)
                                y: 0
                                width: selectorTrack.cellW
                                height: selectorTrack.height

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 9
                                    color: "#FFFFFF"
                                    opacity: (!segCell.isSelected && segMouse.containsMouse) ? 0.06 : 0
                                }

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 2

                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: segCell.modelData.sym
                                        font.pixelSize: 16
                                    }

                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: segCell.modelData.label
                                        font.pixelSize: 13
                                        font.weight: segCell.isSelected ? Font.DemiBold : Font.Normal
                                        color: root.textPrimary
                                    }
                                }

                                MouseArea {
                                    id: segMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: backend.setNoiseByIndex(segCell.modelData.modeIdx)
                                }
                            }
                        }
                    }

                    // ANC Level selector
                    ColumnLayout {
                        visible: backend.supportsAncLevels && backend.noiseModeIndex === 0
                        Layout.fillWidth: true
                        spacing: 4

                        Rectangle { height: 1; Layout.fillWidth: true; color: root.cardBorder; Layout.bottomMargin: 4 }

                        Label { text: root.i18n.anc_level_title; color: root.textPrimary; font.weight: Font.Medium }
                        ComboBox {
                            id: ancLevelCombo
                            Layout.fillWidth: true
                            model: backend.ancLevelModel
                            textRole: "text"
                            valueRole: "value"
                            delegate: ItemDelegate {
                                required property var modelData
                                required property int index
                                width: ancLevelCombo.width
                                text: modelData.text
                                highlighted: ancLevelCombo.highlightedIndex === index
                            }
                            currentIndex: backend.ancLevelIndex
                            displayText: (backend.ancLevelIndex >= 0 && backend.ancLevelIndex < backend.ancLevelModel.length)
                                         ? backend.ancLevelModel[backend.ancLevelIndex].text : ""
                            onModelChanged: Qt.callLater(function() { ancLevelCombo.currentIndex = backend.ancLevelIndex })
                            onActivated: function(index) { backend.setAncLevelByIndex(index) }
                            Connections {
                                target: backend
                                function onStateChanged() { ancLevelCombo.currentIndex = backend.ancLevelIndex }
                                function onLanguageChanged() { Qt.callLater(function() { ancLevelCombo.currentIndex = backend.ancLevelIndex }) }
                            }
                        }
                        Label { text: root.i18n.anc_level_desc; color: root.textSecondary; font.pixelSize: 12 }
                    }

                    // Transparency Effect selector
                    ColumnLayout {
                        visible: backend.supportsTransEffects && backend.noiseModeIndex === 2
                        Layout.fillWidth: true
                        spacing: 4

                        Rectangle { height: 1; Layout.fillWidth: true; color: root.cardBorder; Layout.bottomMargin: 4 }

                        Label { text: root.i18n.trans_effect_title; color: root.textPrimary; font.weight: Font.Medium }
                        ComboBox {
                            id: transEffectCombo
                            Layout.fillWidth: true
                            model: backend.transEffectModel
                            textRole: "text"
                            valueRole: "value"
                            delegate: ItemDelegate {
                                required property var modelData
                                required property int index
                                width: transEffectCombo.width
                                text: modelData.text
                                highlighted: transEffectCombo.highlightedIndex === index
                            }
                            currentIndex: backend.transEffectIndex
                            displayText: (backend.transEffectIndex >= 0 && backend.transEffectIndex < backend.transEffectModel.length)
                                         ? backend.transEffectModel[backend.transEffectIndex].text : ""
                            onModelChanged: Qt.callLater(function() { transEffectCombo.currentIndex = backend.transEffectIndex })
                            onActivated: function(index) { backend.setTransEffectByIndex(index) }
                            Connections {
                                target: backend
                                function onStateChanged() { transEffectCombo.currentIndex = backend.transEffectIndex }
                                function onLanguageChanged() { Qt.callLater(function() { transEffectCombo.currentIndex = backend.transEffectIndex }) }
                            }
                        }
                        Label { text: root.i18n.trans_effect_desc; color: root.textSecondary; font.pixelSize: 12 }
                    }
                }
            }

            // ---- 4. Sound & Controls Section ----
            Label {
                visible: backend.connected && (backend.supportsEq || backend.supportsNoiseCycle || backend.supportsGameMode || backend.supportsWearDetection)
                text: root.i18n.section_sound
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: root.textPrimary
                Layout.topMargin: 12
                Layout.bottomMargin: 10
                Layout.leftMargin: 4
            }

            Rectangle {
                visible: backend.connected && (backend.supportsEq || backend.supportsNoiseCycle || backend.supportsGameMode || backend.supportsWearDetection)
                Layout.fillWidth: true
                implicitHeight: soundCol.implicitHeight + 28
                radius: 12
                color: root.cardColor
                border.color: root.cardBorder
                border.width: 1
                opacity: backend.connected ? 1.0 : 0.55
                enabled: backend.connected

                ColumnLayout {
                    id: soundCol
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    ColumnLayout {
                        visible: backend.supportsEq
                        Layout.fillWidth: true
                        spacing: 6

                        Label { text: root.i18n.eq_title; color: root.textPrimary; font.weight: Font.Medium }
                        ComboBox {
                            id: eqCombo
                            Layout.fillWidth: true
                            model: backend.eqModel
                            textRole: "text"
                            valueRole: "value"
                            delegate: ItemDelegate {
                                required property var modelData
                                required property int index
                                width: eqCombo.width
                                text: modelData.text
                                highlighted: eqCombo.highlightedIndex === index
                            }
                            currentIndex: backend.eqIndex
                            displayText: (backend.eqIndex >= 0 && backend.eqIndex < backend.eqModel.length)
                                         ? backend.eqModel[backend.eqIndex].text : ""
                            onModelChanged: Qt.callLater(function() { eqCombo.currentIndex = backend.eqIndex })
                            onActivated: function(index) { backend.setEqByIndex(index) }
                            Connections {
                                target: backend
                                function onStateChanged() { eqCombo.currentIndex = backend.eqIndex }
                                function onLanguageChanged() { Qt.callLater(function() { eqCombo.currentIndex = backend.eqIndex }) }
                            }
                        }
                        Label { text: root.i18n.eq_desc; color: root.textSecondary; font.pixelSize: 12 }
                    }

                    Rectangle {
                        visible: backend.supportsEq && (backend.supportsNoiseCycle || backend.supportsGameMode || backend.supportsWearDetection)
                        height: 1
                        Layout.fillWidth: true
                        color: root.cardBorder
                    }

                    ColumnLayout {
                        visible: backend.supportsNoiseCycle
                        Layout.fillWidth: true
                        spacing: 6

                        Label { text: root.i18n.cycle_title; color: root.textPrimary; font.weight: Font.Medium }
                        ComboBox {
                            id: cycleCombo
                            Layout.fillWidth: true
                            model: backend.cycleModel
                            textRole: "text"
                            valueRole: "value"
                            delegate: ItemDelegate {
                                required property var modelData
                                required property int index
                                width: cycleCombo.width
                                text: modelData.text
                                highlighted: cycleCombo.highlightedIndex === index
                            }
                            currentIndex: backend.cycleIndex
                            displayText: (backend.cycleIndex >= 0 && backend.cycleIndex < backend.cycleModel.length)
                                         ? backend.cycleModel[backend.cycleIndex].text : ""
                            onModelChanged: Qt.callLater(function() { cycleCombo.currentIndex = backend.cycleIndex })
                            onActivated: function(index) { backend.setCycleByIndex(index) }
                            Connections {
                                target: backend
                                function onStateChanged() { cycleCombo.currentIndex = backend.cycleIndex }
                                function onLanguageChanged() { Qt.callLater(function() { cycleCombo.currentIndex = backend.cycleIndex }) }
                            }
                        }
                        Label { text: root.i18n.cycle_desc; color: root.textSecondary; font.pixelSize: 12 }
                    }

                    Rectangle {
                        visible: backend.supportsNoiseCycle && (backend.supportsGameMode || backend.supportsWearDetection)
                        height: 1
                        Layout.fillWidth: true
                        color: root.cardBorder
                    }

                    RowLayout {
                        visible: backend.supportsGameMode
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { text: root.i18n.game_title; color: root.textPrimary; font.weight: Font.Medium }
                            Label { text: root.i18n.game_desc; color: root.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                        Switch {
                            checked: backend.gameMode
                            onToggled: backend.setGameMode(checked)
                        }
                    }

                    Rectangle {
                        visible: backend.supportsGameMode && backend.supportsWearDetection
                        height: 1
                        Layout.fillWidth: true
                        color: root.cardBorder
                    }

                    RowLayout {
                        visible: backend.supportsWearDetection
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { text: root.i18n.wear_detect_title; color: root.textPrimary; font.weight: Font.Medium }
                            Label { text: root.i18n.wear_detect_desc; color: root.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                        Switch {
                            checked: backend.wearDetection
                            onToggled: backend.setWearDetection(checked)
                        }
                    }
                }
            }

            // ---- 5. My Earbuds Section ----
            Label {
                visible: backend.connected
                text: root.i18n.section_devices
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: root.textPrimary
                Layout.topMargin: 12
                Layout.bottomMargin: 10
                Layout.leftMargin: 4
            }

            Rectangle {
                visible: backend.connected
                Layout.fillWidth: true
                implicitHeight: devsCol.implicitHeight + 28
                radius: 12
                color: root.cardColor
                border.color: root.cardBorder
                border.width: 1

                ColumnLayout {
                    id: devsCol
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    Repeater {
                        model: backend.savedDevices

                        delegate: ColumnLayout {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle { visible: index > 0; height: 1; Layout.fillWidth: true; color: root.cardBorder }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                Label {
                                    text: modelData.active ? "✓" : "🎧"
                                    font.pixelSize: 16
                                    color: modelData.active ? root.accentBlue : root.textSecondary
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label { text: modelData.displayName; color: root.textPrimary; font.weight: Font.Medium }
                                    Label {
                                        text: modelData.mac + " · " + (modelData.btConnected ? root.i18n.status_connected_bt : root.i18n.status_offline)
                                        color: root.textSecondary
                                        font.pixelSize: 12
                                    }
                                }

                                Button {
                                    visible: !modelData.active
                                    text: root.i18n.action_select
                                    onClicked: backend.selectSavedDevice(modelData.mac)
                                }

                                ToolButton {
                                    text: "✎"
                                    ToolTip.visible: hovered
                                    ToolTip.text: root.i18n.action_rename
                                    onClicked: {
                                        renameDialog.targetMac = modelData.mac
                                        renameField.text = modelData.alias.length > 0 ? modelData.alias : modelData.displayName
                                        renameDialog.open()
                                    }
                                }

                                ToolButton {
                                    text: "✕"
                                    ToolTip.visible: hovered
                                    ToolTip.text: root.i18n.action_remove_from_list
                                    onClicked: backend.removeSavedDevice(modelData.mac)
                                }
                            }
                        }
                    }

                    Rectangle { visible: backend.savedDevices.length > 0; height: 1; Layout.fillWidth: true; color: root.cardBorder }

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { text: root.i18n.auto_switch_title; color: root.textPrimary; font.weight: Font.Medium }
                            Label { text: root.i18n.auto_switch_desc; color: root.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                        Switch {
                            checked: backend.autoSwitch
                            onToggled: backend.setAutoSwitch(checked)
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "+ " + root.i18n.action_add_earbuds_ellipsis
                        onClicked: {
                            backend.scanBluetoothDevices()
                            addDeviceDialog.open()
                        }
                    }
                }
            }

            // ---- 6. Multipoint Section ----
            Label {
                visible: backend.connected && backend.supportsMultipoint && backend.multipointDevices.length > 0
                text: root.i18n.section_multipoint
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: root.textPrimary
                Layout.topMargin: 12
                Layout.bottomMargin: 10
                Layout.leftMargin: 4
            }

            Rectangle {
                visible: backend.connected && backend.supportsMultipoint && backend.multipointDevices.length > 0
                Layout.fillWidth: true
                implicitHeight: mpCol.implicitHeight + 28
                radius: 12
                color: root.cardColor
                border.color: root.cardBorder
                border.width: 1

                ColumnLayout {
                    id: mpCol
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    Repeater {
                        model: backend.multipointDevices

                        delegate: ColumnLayout {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle { visible: index > 0; height: 1; Layout.fillWidth: true; color: root.cardBorder }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                Label {
                                    text: modelData.connected ? "●" : "○"
                                    color: modelData.connected ? "#34D399" : root.textSecondary
                                    font.pixelSize: 15
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label { text: modelData.name; color: root.textPrimary; font.weight: Font.Medium }
                                    Label { text: modelData.mac + " · " + modelData.statusText; color: root.textSecondary; font.pixelSize: 12 }
                                }

                                Button {
                                    visible: !modelData.active
                                    text: modelData.connected ? root.i18n.action_disconnect : root.i18n.action_connect
                                    onClicked: backend.toggleMultipointDevice(modelData.mac, !modelData.connected)
                                }
                            }
                        }
                    }
                }
            }

            // ---- 7. Find Device Section ----
            Label {
                visible: backend.connected && backend.supportsFindDevice
                text: root.i18n.section_find
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: root.textPrimary
                Layout.topMargin: 12
                Layout.bottomMargin: 10
                Layout.leftMargin: 4
            }

            Rectangle {
                visible: backend.connected && backend.supportsFindDevice
                Layout.fillWidth: true
                implicitHeight: findRow.implicitHeight + 24
                radius: 12
                color: root.cardColor
                border.color: root.cardBorder
                border.width: 1
                opacity: backend.connected ? 1.0 : 0.55
                enabled: backend.connected

                RowLayout {
                    id: findRow
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { text: backend.isRinging ? root.i18n.find_stop : root.i18n.find_start; color: root.textPrimary; font.weight: Font.Medium }
                        Label { text: root.i18n.find_desc; color: root.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }

                    Button {
                        text: backend.isRinging ? "■" : "🔔"
                        onClicked: {
                            if (backend.isRinging) backend.stopFindEarbuds()
                            else findConfirmDialog.open()
                        }
                    }
                }
            }

            // ---- Subtle Version Footer ----
            Label {
                id: winFooter
                Layout.fillWidth: true
                Layout.topMargin: backend.connected ? 14 : 0
                Layout.bottomMargin: 12
                horizontalAlignment: Text.AlignHCenter
                text: backend.appVersion
                font.pixelSize: 11
                color: root.textSecondary
                opacity: 0.55
            }
        }
    }

    // ---- Dialogs ----

    Dialog {
        id: addDeviceDialog
        title: root.i18n.dlg_add_title
        modal: true
        anchors.centerIn: parent
        width: Math.min(root.width - 40, 520)
        standardButtons: Dialog.Close

        contentItem: ColumnLayout {
            spacing: 10

            Label {
                text: root.i18n.dlg_add_hint
                color: root.textSecondary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ScrollView {
                id: winDiscoveredScroll
                visible: backend.discoveredDevices.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(winDiscoveredCol.implicitHeight, Math.max(150, Math.round(root.height * 0.34)))
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                ColumnLayout {
                    id: winDiscoveredCol
                    width: winDiscoveredScroll.availableWidth
                    spacing: 8

                    Repeater {
                        model: backend.discoveredDevices

                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Label { text: modelData.name; color: root.textPrimary; font.weight: Font.Medium; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: modelData.subtitle; color: root.textSecondary; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                            }

                            Button {
                                text: modelData.alreadySaved ? root.i18n.action_select : root.i18n.action_add
                                onClicked: {
                                    backend.addSavedDevice(modelData.mac, modelData.name, "")
                                    addDeviceDialog.close()
                                }
                            }
                        }
                    }
                }
            }

            Button {
                Layout.fillWidth: true
                text: root.i18n.dlg_refresh_bt
                onClicked: backend.scanBluetoothDevices()
            }

            Label { text: root.i18n.dlg_mac_label; color: root.textPrimary; font.pixelSize: 12 }
            TextField {
                id: manualMacField
                Layout.fillWidth: true
                placeholderText: "1C:BA:B8:0F:5A:0F"
            }

            Label { text: root.i18n.dlg_alias_label; color: root.textPrimary; font.pixelSize: 12 }
            TextField {
                id: manualAliasField
                Layout.fillWidth: true
                placeholderText: root.i18n.dlg_alias_placeholder
            }

            Button {
                Layout.fillWidth: true
                text: root.i18n.dlg_add_by_mac
                enabled: manualMacField.text.trim().length >= 17
                onClicked: {
                    if (backend.addSavedDevice(manualMacField.text, manualAliasField.text, manualAliasField.text)) {
                        manualMacField.text = ""
                        manualAliasField.text = ""
                        addDeviceDialog.close()
                    }
                }
            }

            Button {
                Layout.fillWidth: true
                text: root.i18n.dlg_pair_kde
                onClicked: backend.openSystemBluetoothSettings()
            }
        }
    }

    Dialog {
        id: renameDialog
        property string targetMac: ""
        title: root.i18n.dlg_rename_title
        modal: true
        anchors.centerIn: parent
        width: Math.min(root.width - 40, 400)
        standardButtons: Dialog.Save | Dialog.Cancel

        onAccepted: {
            if (targetMac.length > 0) backend.renameSavedDevice(targetMac, renameField.text)
        }

        contentItem: ColumnLayout {
            spacing: 8
            Label { text: root.i18n.dlg_rename_label; color: root.textPrimary }
            TextField {
                id: renameField
                Layout.fillWidth: true
                placeholderText: root.i18n.dlg_rename_placeholder
            }
        }
    }

    Dialog {
        id: findConfirmDialog
        title: root.i18n.find_warn_title
        modal: true
        anchors.centerIn: parent
        width: Math.min(root.width - 40, 400)
        standardButtons: Dialog.Yes | Dialog.Cancel
        onAccepted: backend.startFindEarbuds()

        contentItem: ColumnLayout {
            spacing: 6
            Label { text: root.i18n.find_warn_heading; color: root.textPrimary; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: root.i18n.find_warn_body; color: root.textSecondary; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }

    Dialog {
        id: probeDialog
        title: root.i18n.dlg_probe_title
        modal: true
        anchors.centerIn: parent
        width: Math.min(root.width - 40, 480)
        standardButtons: Dialog.Close

        contentItem: ColumnLayout {
            spacing: 10
            Label { text: root.i18n.dlg_probe_hint; color: root.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 220
                TextArea {
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    font.family: "Consolas"
                    font.pixelSize: 11
                    text: backend.probeReport
                }
            }
            Button {
                Layout.fillWidth: true
                enabled: !backend.probeBusy && backend.probeReport.length > 0
                text: root.i18n.dlg_probe_copy
                onClicked: {
                    backend.copyReportToClipboard()
                    probeDialog.close()
                }
            }
        }
    }
}
