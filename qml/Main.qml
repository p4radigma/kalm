// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.kirigamiaddons.delegates as Delegates

Kirigami.ApplicationWindow {
    id: root

    readonly property var i18n: backend.i18n
    readonly property real cardMaxW: Kirigami.Units.gridUnit * 35

    title: (backend.connected && backend.deviceName) ? (backend.deviceName + " — Kalm") : "Kalm"
    width: Kirigami.Units.gridUnit * 38
    height: Kirigami.Units.gridUnit * 38
    minimumWidth: Kirigami.Units.gridUnit * 30
    minimumHeight: Kirigami.Units.gridUnit * 28

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

    // ---- Sidebar Drawer ----
    globalDrawer: Kirigami.GlobalDrawer {
        id: drawer
        title: "Kalm"
        titleIcon: "audio-headset"
        isMenu: false
        modal: true
        handleClosedToolTip: root.i18n.drawer_open
        handleOpenToolTip: root.i18n.drawer_close

        topContent: [
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                Layout.topMargin: Kirigami.Units.smallSpacing
                Layout.bottomMargin: Kirigami.Units.smallSpacing
                spacing: Kirigami.Units.smallSpacing

                Controls.Label {
                    text: root.i18n.lang_title
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }

                Controls.ComboBox {
                    id: drawerLangCombo
                    Layout.fillWidth: true
                    model: backend.languageModel
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: backend.languageIndex
                    displayText: (backend.languageIndex >= 0 && backend.languageIndex < backend.languageModel.length)
                                 ? backend.languageModel[backend.languageIndex].text : ""
                    onModelChanged: Qt.callLater(function() { drawerLangCombo.currentIndex = backend.languageIndex })
                    onActivated: function(index) {
                        backend.setLanguageByIndex(index)
                    }
                    Connections {
                        target: backend
                        function onLanguageChanged() {
                            Qt.callLater(function() {
                                drawerLangCombo.currentIndex = backend.languageIndex
                            })
                        }
                    }
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.smallSpacing
                }
            }
        ]

        actions: [
            Kirigami.Action {
                text: root.i18n.action_add_earbuds_ellipsis
                icon.name: "list-add-symbolic"
                onTriggered: {
                    backend.scanBluetoothDevices()
                    addDeviceDialog.open()
                }
            },
            Kirigami.Action {
                text: root.i18n.action_kde_bt_settings
                icon.name: "preferences-system-bluetooth"
                onTriggered: backend.openSystemBluetoothSettings()
            },
            Kirigami.Action {
                text: root.i18n.action_probe
                icon.name: "tools-report-bug"
                visible: backend.supportsProbe
                onTriggered: {
                    backend.runDiagnosticProbe()
                    probeDialog.open()
                }
            },
            Kirigami.Action {
                separator: true
            },
            Kirigami.Action {
                text: root.i18n.action_minimize_tray
                icon.name: "window-minimize-symbolic"
                onTriggered: root.visible = false
            },
            Kirigami.Action {
                text: root.i18n.action_quit
                icon.name: "application-exit-symbolic"
                onTriggered: backend.quitApp()
            }
        ]
    }

    // ---- Main Kirigami FormCard Page ----
    pageStack.initialPage: FormCard.FormCardPage {
        id: mainPage
        title: "Kalm"
        bottomPadding: 0
        verticalScrollBarPolicy: Controls.ScrollBar.AlwaysOff

        actions: [
            Kirigami.Action {
                text: root.i18n.action_add_earbuds
                icon.name: "list-add-symbolic"
                onTriggered: {
                    backend.scanBluetoothDevices()
                    addDeviceDialog.open()
                }
            },
            Kirigami.Action {
                visible: backend.connected
                text: root.i18n.action_refresh
                icon.name: "view-refresh-symbolic"
                onTriggered: backend.refreshNow()
            }
        ]

        // Inline status/error message banner (Loader ensures Kirigami's internal
        // closeButton `root.visible = false` never breaks re-showing future banners)
        Loader {
            id: inlineBanner
            active: backend.bannerText.length > 0
            visible: active
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.largeSpacing

            sourceComponent: Kirigami.InlineMessage {
                width: inlineBanner.width
                visible: true
                text: backend.bannerText
                type: {
                    if (backend.bannerType === 1) return Kirigami.MessageType.Positive
                    if (backend.bannerType === 2) return Kirigami.MessageType.Warning
                    if (backend.bannerType === 3) return Kirigami.MessageType.Error
                    return Kirigami.MessageType.Information
                }
                showCloseButton: true
                onVisibleChanged: {
                    if (!visible && backend.bannerText.length > 0) {
                        backend.clearBanner()
                    }
                }
            }
        }

        // ---- Centered Empty State When Disconnected ----
        Item {
            visible: !backend.connected
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(
                Kirigami.Units.gridUnit * 14,
                mainPage.flickable.height
                - (inlineBanner.visible ? (inlineBanner.height + Kirigami.Units.largeSpacing) : 0)
                - versionFooter.implicitHeight
                - Kirigami.Units.largeSpacing
            )

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - Kirigami.Units.largeSpacing * 4, Kirigami.Units.gridUnit * 22)
                spacing: Kirigami.Units.smallSpacing + 2

                Kirigami.Icon {
                    source: "audio-headset"
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: Kirigami.Units.iconSizes.large
                    implicitHeight: Kirigami.Units.iconSizes.large
                    opacity: 0.45
                }

                Kirigami.Heading {
                    level: 2
                    text: root.i18n.empty_connect_title
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Controls.Label {
                    text: root.i18n.empty_connect_desc
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    opacity: 0.65
                    Layout.fillWidth: true
                }

                Controls.Button {
                    visible: backend.deviceMac.length > 0
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    text: root.i18n.action_connect_bt + " (" + backend.deviceName + ")"
                    icon.name: "network-bluetooth-activated-symbolic"
                    onClicked: backend.connectBluetoothDevice(backend.deviceMac)
                }
            }
        }

        // ---- Connection Status Header Card (Centered) ----
        FormCard.FormCard {
            visible: backend.connected
            maximumWidth: root.cardMaxW
            Layout.topMargin: Kirigami.Units.largeSpacing

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing
                Layout.bottomMargin: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: "audio-headset"
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: Kirigami.Units.iconSizes.medium
                    implicitHeight: Kirigami.Units.iconSizes.medium
                    opacity: backend.connected ? 1.0 : 0.55
                }

                Kirigami.Heading {
                    level: 2
                    text: backend.deviceName || root.i18n.status_no_device_selected
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Controls.Label {
                    text: backend.statusSummary
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    opacity: 0.75
                    Layout.fillWidth: true
                }

                Controls.Button {
                    visible: !backend.connected && backend.deviceMac.length > 0
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    text: root.i18n.action_connect_bt
                    icon.name: "network-bluetooth-activated-symbolic"
                    onClicked: backend.connectBluetoothDevice(backend.deviceMac)
                }
            }
        }

        // ---- Battery Info Grid (Left / Right / Case) ----
        FormCard.FormGridContainer {
            visible: backend.connected
            maximumWidth: root.cardMaxW
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing

            infoCards: [
                FormCard.FormGridContainer.InfoCard {
                    title: backend.leftBatteryText
                    subtitle: backend.leftBatterySub
                },
                FormCard.FormGridContainer.InfoCard {
                    title: backend.rightBatteryText
                    subtitle: backend.rightBatterySub
                },
                FormCard.FormGridContainer.InfoCard {
                    title: backend.caseBatteryText
                    subtitle: backend.caseBatterySub
                }
            ]
        }

        // ---- Primary Section: Noise Cancellation Selector + ANC Level / Transparency Mode ----
        FormCard.FormHeader {
            visible: backend.connected
            maximumWidth: root.cardMaxW
            title: root.i18n.section_noise
            topPadding: Kirigami.Units.largeSpacing
            bottomPadding: Kirigami.Units.largeSpacing + 2
        }

        FormCard.FormCard {
            visible: backend.connected
            maximumWidth: root.cardMaxW
            enabled: backend.connected

            // Custom pixel-centered 3-segment Kirigami selector (avoids upstream RadioSelector asymmetric margins)
            FormCard.AbstractFormDelegate {
                id: noiseSelectorDelegate
                background: null
                topPadding: Kirigami.Units.largeSpacing
                bottomPadding: Kirigami.Units.largeSpacing
                leftPadding: Kirigami.Units.largeSpacing
                rightPadding: Kirigami.Units.largeSpacing

                contentItem: Item {
                    id: selectorTrack
                    implicitHeight: Kirigami.Units.gridUnit * 2.9

                    readonly property real gap: Kirigami.Units.smallSpacing
                    readonly property real cellW: Math.max(0, (width - gap * 2) / 3)
                    readonly property var modeItems: [
                        { label: root.i18n.mode_anc, iconName: "microphone-sensitivity-muted-symbolic", modeIdx: 0 },
                        { label: root.i18n.mode_off, iconName: "audio-headphones-symbolic", modeIdx: 1 },
                        { label: root.i18n.mode_trans, iconName: "microphone-sensitivity-high-symbolic", modeIdx: 2 }
                    ]

                    Kirigami.ShadowedRectangle {
                        id: activePill
                        visible: backend.noiseModeIndex >= 0 && backend.noiseModeIndex <= 2
                        x: Math.max(0, backend.noiseModeIndex) * (selectorTrack.cellW + selectorTrack.gap)
                        y: 0
                        width: selectorTrack.cellW
                        height: selectorTrack.height
                        radius: Kirigami.Units.cornerRadius

                        color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.hoverColor, Kirigami.Theme.backgroundColor, 0.8)
                        border {
                            width: 1
                            color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.hoverColor, Kirigami.Theme.backgroundColor, 0.5)
                        }
                        shadow {
                            size: 7
                            yOffset: 2
                            color: Qt.rgba(0, 0, 0, 0.15)
                        }

                        Behavior on x {
                            NumberAnimation {
                                duration: Kirigami.Units.longDuration
                                easing.type: Easing.InOutCubic
                            }
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
                                radius: Kirigami.Units.cornerRadius
                                color: Kirigami.Theme.textColor
                                opacity: (!segCell.isSelected && segMouse.containsMouse) ? 0.08 : 0

                                Behavior on opacity {
                                    NumberAnimation { duration: Kirigami.Units.shortDuration }
                                }
                            }

                            Column {
                                id: segContent
                                anchors.centerIn: parent
                                spacing: 3

                                Kirigami.Icon {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    source: segCell.modelData.iconName
                                    width: Kirigami.Units.iconSizes.smallMedium
                                    height: Kirigami.Units.iconSizes.smallMedium
                                    color: segCell.isSelected ? Kirigami.Theme.hoverColor : Kirigami.Theme.textColor
                                }

                                Controls.Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    horizontalAlignment: Text.AlignHCenter
                                    text: segCell.modelData.label
                                    font.weight: segCell.isSelected ? Font.DemiBold : Font.Normal
                                    color: Kirigami.Theme.textColor
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
            }

            FormCard.FormDelegateSeparator {
                visible: backend.supportsAncLevels && backend.noiseModeIndex === 0
            }

            FormCard.FormComboBoxDelegate {
                id: ancLevelCombo
                comboBoxDelegate: Delegates.RoundedItemDelegate {
                    implicitWidth: ListView.view ? ListView.view.width : Kirigami.Units.gridUnit * 16
                    text: backend.ancLevelModel[index] ? backend.ancLevelModel[index].text : ""
                    highlighted: backend.ancLevelIndex === index
                }
                visible: backend.supportsAncLevels && backend.noiseModeIndex === 0
                text: root.i18n.anc_level_title
                description: root.i18n.anc_level_desc
                model: backend.ancLevelModel
                textRole: "text"
                valueRole: "value"
                currentIndex: backend.ancLevelIndex
                displayText: (backend.ancLevelIndex >= 0 && backend.ancLevelIndex < backend.ancLevelModel.length)
                             ? backend.ancLevelModel[backend.ancLevelIndex].text : ""
                onModelChanged: Qt.callLater(function() { ancLevelCombo.currentIndex = backend.ancLevelIndex })
                onActivated: function(index) {
                    backend.setAncLevelByIndex(index)
                }
                Connections {
                    target: backend
                    function onStateChanged() { ancLevelCombo.currentIndex = backend.ancLevelIndex }
                    function onLanguageChanged() { Qt.callLater(function() { ancLevelCombo.currentIndex = backend.ancLevelIndex }) }
                }
            }

            FormCard.FormDelegateSeparator {
                visible: backend.supportsTransEffects && backend.noiseModeIndex === 2
            }

            FormCard.FormComboBoxDelegate {
                id: transEffectCombo
                comboBoxDelegate: Delegates.RoundedItemDelegate {
                    implicitWidth: ListView.view ? ListView.view.width : Kirigami.Units.gridUnit * 16
                    text: backend.transEffectModel[index] ? backend.transEffectModel[index].text : ""
                    highlighted: backend.transEffectIndex === index
                }
                visible: backend.supportsTransEffects && backend.noiseModeIndex === 2
                text: root.i18n.trans_effect_title
                description: root.i18n.trans_effect_desc
                model: backend.transEffectModel
                textRole: "text"
                valueRole: "value"
                currentIndex: backend.transEffectIndex
                displayText: (backend.transEffectIndex >= 0 && backend.transEffectIndex < backend.transEffectModel.length)
                             ? backend.transEffectModel[backend.transEffectIndex].text : ""
                onModelChanged: Qt.callLater(function() { transEffectCombo.currentIndex = backend.transEffectIndex })
                onActivated: function(index) {
                    backend.setTransEffectByIndex(index)
                }
                Connections {
                    target: backend
                    function onStateChanged() { transEffectCombo.currentIndex = backend.transEffectIndex }
                    function onLanguageChanged() { Qt.callLater(function() { transEffectCombo.currentIndex = backend.transEffectIndex }) }
                }
            }
        }

        // ---- Sound & Controls Section ----
        FormCard.FormHeader {
            maximumWidth: root.cardMaxW
            visible: backend.connected && (backend.supportsEq || backend.supportsNoiseCycle || backend.supportsGameMode || backend.supportsWearDetection)
            title: root.i18n.section_sound
            topPadding: Kirigami.Units.largeSpacing
            bottomPadding: Kirigami.Units.largeSpacing + 2
        }

        FormCard.FormCard {
            maximumWidth: root.cardMaxW
            visible: backend.connected && (backend.supportsEq || backend.supportsNoiseCycle || backend.supportsGameMode || backend.supportsWearDetection)
            enabled: backend.connected

            FormCard.FormComboBoxDelegate {
                id: eqCombo
                comboBoxDelegate: Delegates.RoundedItemDelegate {
                    implicitWidth: ListView.view ? ListView.view.width : Kirigami.Units.gridUnit * 16
                    text: backend.eqModel[index] ? backend.eqModel[index].text : ""
                    highlighted: backend.eqIndex === index
                }
                visible: backend.supportsEq
                text: root.i18n.eq_title
                description: root.i18n.eq_desc
                model: backend.eqModel
                textRole: "text"
                valueRole: "value"
                currentIndex: backend.eqIndex
                displayText: (backend.eqIndex >= 0 && backend.eqIndex < backend.eqModel.length)
                             ? backend.eqModel[backend.eqIndex].text : ""
                onModelChanged: Qt.callLater(function() { eqCombo.currentIndex = backend.eqIndex })
                onActivated: function(index) {
                    backend.setEqByIndex(index)
                }
                Connections {
                    target: backend
                    function onStateChanged() { eqCombo.currentIndex = backend.eqIndex }
                    function onLanguageChanged() { Qt.callLater(function() { eqCombo.currentIndex = backend.eqIndex }) }
                }
            }

            FormCard.FormDelegateSeparator {
                visible: backend.supportsEq && (backend.supportsNoiseCycle || backend.supportsGameMode || backend.supportsWearDetection)
                above: eqCombo
                below: backend.supportsNoiseCycle ? cycleCombo : (backend.supportsGameMode ? gameSwitch : wearSwitch)
            }

            FormCard.FormComboBoxDelegate {
                id: cycleCombo
                comboBoxDelegate: Delegates.RoundedItemDelegate {
                    implicitWidth: ListView.view ? ListView.view.width : Kirigami.Units.gridUnit * 16
                    text: backend.cycleModel[index] ? backend.cycleModel[index].text : ""
                    highlighted: backend.cycleIndex === index
                }
                visible: backend.supportsNoiseCycle
                text: root.i18n.cycle_title
                description: root.i18n.cycle_desc
                model: backend.cycleModel
                textRole: "text"
                valueRole: "value"
                currentIndex: backend.cycleIndex
                displayText: (backend.cycleIndex >= 0 && backend.cycleIndex < backend.cycleModel.length)
                             ? backend.cycleModel[backend.cycleIndex].text : ""
                onModelChanged: Qt.callLater(function() { cycleCombo.currentIndex = backend.cycleIndex })
                onActivated: function(index) {
                    backend.setCycleByIndex(index)
                }
                Connections {
                    target: backend
                    function onStateChanged() { cycleCombo.currentIndex = backend.cycleIndex }
                    function onLanguageChanged() { Qt.callLater(function() { cycleCombo.currentIndex = backend.cycleIndex }) }
                }
            }

            FormCard.FormDelegateSeparator {
                visible: backend.supportsNoiseCycle && (backend.supportsGameMode || backend.supportsWearDetection)
                above: cycleCombo
                below: backend.supportsGameMode ? gameSwitch : wearSwitch
            }

            FormCard.FormSwitchDelegate {
                id: gameSwitch
                visible: backend.supportsGameMode
                text: root.i18n.game_title
                description: root.i18n.game_desc
                checked: backend.gameMode
                onToggled: backend.setGameMode(checked)
            }

            FormCard.FormDelegateSeparator {
                visible: backend.supportsGameMode && backend.supportsWearDetection
                above: gameSwitch
                below: wearSwitch
            }

            FormCard.FormSwitchDelegate {
                id: wearSwitch
                visible: backend.supportsWearDetection
                text: root.i18n.wear_detect_title
                description: root.i18n.wear_detect_desc
                checked: backend.wearDetection
                onToggled: backend.setWearDetection(checked)
            }
        }

        // ---- Saved Earbuds Section (Multi-Device Management) ----
        FormCard.FormHeader {
            visible: backend.connected
            maximumWidth: root.cardMaxW
            title: root.i18n.section_devices
            topPadding: Kirigami.Units.largeSpacing
            bottomPadding: Kirigami.Units.largeSpacing + 2
        }

        FormCard.FormCard {
            visible: backend.connected
            maximumWidth: root.cardMaxW

            Repeater {
                model: backend.savedDevices

                delegate: ColumnLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 0

                    FormCard.FormDelegateSeparator {
                        visible: index > 0
                    }

                    FormCard.FormTextDelegate {
                        text: modelData.displayName
                        description: modelData.mac + " · " + (modelData.btConnected ? root.i18n.status_connected_bt : root.i18n.status_offline)
                        leadingPadding: Kirigami.Units.largeSpacing * 1.5

                        leading: Kirigami.Icon {
                            source: modelData.active ? "emblem-checked" : "audio-headset"
                            implicitWidth: Kirigami.Units.iconSizes.smallMedium
                            implicitHeight: Kirigami.Units.iconSizes.smallMedium
                            Layout.leftMargin: Kirigami.Units.smallSpacing
                        }

                        trailing: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Controls.Button {
                                visible: !modelData.active
                                text: root.i18n.action_select
                                onClicked: backend.selectSavedDevice(modelData.mac)
                            }

                            Controls.ToolButton {
                                icon.name: "edit-rename-symbolic"
                                Controls.ToolTip.visible: hovered
                                Controls.ToolTip.text: root.i18n.action_rename
                                onClicked: {
                                    renameDialog.targetMac = modelData.mac
                                    renameField.text = modelData.alias.length > 0 ? modelData.alias : modelData.displayName
                                    renameDialog.open()
                                }
                            }

                            Controls.ToolButton {
                                icon.name: "edit-delete-remove-symbolic"
                                Controls.ToolTip.visible: hovered
                                Controls.ToolTip.text: root.i18n.action_remove_from_list
                                onClicked: backend.removeSavedDevice(modelData.mac)
                            }
                        }
                    }
                }
            }

            FormCard.FormDelegateSeparator {
                visible: backend.savedDevices.length > 0
            }

            FormCard.FormSwitchDelegate {
                text: root.i18n.auto_switch_title
                description: root.i18n.auto_switch_desc
                checked: backend.autoSwitch
                onToggled: backend.setAutoSwitch(checked)
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormButtonDelegate {
                text: root.i18n.action_add_earbuds_ellipsis
                icon.name: "list-add-symbolic"
                onClicked: {
                    backend.scanBluetoothDevices()
                    addDeviceDialog.open()
                }
            }
        }

        // ---- Dual-Connection Multipoint Section ----
        FormCard.FormHeader {
            maximumWidth: root.cardMaxW
            title: root.i18n.section_multipoint
            topPadding: Kirigami.Units.largeSpacing
            bottomPadding: Kirigami.Units.largeSpacing + 2
            visible: backend.supportsMultipoint && backend.connected && backend.multipointDevices.length > 0
        }

        FormCard.FormCard {
            maximumWidth: root.cardMaxW
            visible: backend.supportsMultipoint && backend.connected && backend.multipointDevices.length > 0

            Repeater {
                model: backend.multipointDevices

                delegate: ColumnLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 0

                    FormCard.FormDelegateSeparator {
                        visible: index > 0
                    }

                    FormCard.FormTextDelegate {
                        text: modelData.name
                        description: modelData.mac + " · " + modelData.statusText
                        leadingPadding: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing

                        leading: Kirigami.Icon {
                            source: modelData.connected ? "network-connect-symbolic" : "network-disconnect-symbolic"
                            implicitWidth: Kirigami.Units.iconSizes.smallMedium
                            implicitHeight: Kirigami.Units.iconSizes.smallMedium
                        }

                        trailing: Controls.Button {
                            visible: !modelData.active
                            text: modelData.connected ? root.i18n.action_disconnect : root.i18n.action_connect
                            onClicked: backend.toggleMultipointDevice(modelData.mac, !modelData.connected)
                        }
                    }
                }
            }
        }

        // ---- Find My Earbuds Section ----
        FormCard.FormHeader {
            maximumWidth: root.cardMaxW
            visible: backend.connected && backend.supportsFindDevice
            title: root.i18n.section_find
            topPadding: Kirigami.Units.largeSpacing
            bottomPadding: Kirigami.Units.largeSpacing + 2
        }

        FormCard.FormCard {
            maximumWidth: root.cardMaxW
            visible: backend.connected && backend.supportsFindDevice
            enabled: backend.connected

            FormCard.FormButtonDelegate {
                text: backend.isRinging ? root.i18n.find_stop : root.i18n.find_start
                description: root.i18n.find_desc
                icon.name: backend.isRinging ? "media-playback-stop-symbolic" : "preferences-desktop-notification-bell"
                onClicked: {
                    if (backend.isRinging) {
                        backend.stopFindEarbuds()
                    } else {
                        findConfirmDialog.open()
                    }
                }
            }
        }

        // ---- Subtle Version Footer ----
        Controls.Label {
            id: versionFooter
            Layout.fillWidth: true
            Layout.topMargin: backend.connected ? Kirigami.Units.largeSpacing : 0
            Layout.bottomMargin: Kirigami.Units.largeSpacing
            horizontalAlignment: Text.AlignHCenter
            text: backend.appVersion
            font.pointSize: Kirigami.Theme.smallFont.pointSize
            opacity: 0.48
        }
    }

    // ---- Dialog: Add Earbuds ----
    FormCard.FormCardDialog {
        id: addDeviceDialog
        title: root.i18n.dlg_add_title
        width: Math.min(root.width - Kirigami.Units.gridUnit * 3, Kirigami.Units.gridUnit * 32)
        standardButtons: Controls.Dialog.Close

        FormCard.FormSectionText {
            text: root.i18n.dlg_add_hint
        }

        FormCard.FormDelegateSeparator {
            visible: backend.discoveredDevices.length > 0
        }

        Controls.ScrollView {
            id: discoveredScroll
            visible: backend.discoveredDevices.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(
                discoveredListCol.implicitHeight,
                Math.max(Kirigami.Units.gridUnit * 7, Math.round(root.height * 0.34))
            )
            clip: true
            Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff

            ColumnLayout {
                id: discoveredListCol
                width: discoveredScroll.availableWidth
                spacing: 0

                Repeater {
                    model: backend.discoveredDevices

                    delegate: ColumnLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: 0

                        FormCard.FormDelegateSeparator {
                            visible: index > 0
                        }

                        FormCard.FormTextDelegate {
                            text: modelData.name
                            description: modelData.subtitle
                            leadingPadding: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing

                            leading: Kirigami.Icon {
                                source: modelData.isVivo ? "audio-headset" : "preferences-system-bluetooth"
                                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                                implicitHeight: Kirigami.Units.iconSizes.smallMedium
                            }

                            trailing: Controls.Button {
                                text: modelData.alreadySaved ? root.i18n.action_select : root.i18n.action_add
                                icon.name: modelData.alreadySaved ? "dialog-ok-apply" : "list-add-symbolic"
                                onClicked: {
                                    backend.addSavedDevice(modelData.mac, modelData.name, "")
                                    addDeviceDialog.close()
                                }
                            }
                        }
                    }
                }
            }
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormButtonDelegate {
            text: root.i18n.dlg_refresh_bt
            icon.name: "view-refresh-symbolic"
            onClicked: backend.scanBluetoothDevices()
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormTextFieldDelegate {
            id: manualMacField
            label: root.i18n.dlg_mac_label
            placeholderText: "1C:BA:B8:0F:5A:0F"
        }

        FormCard.FormTextFieldDelegate {
            id: manualAliasField
            label: root.i18n.dlg_alias_label
            placeholderText: root.i18n.dlg_alias_placeholder
        }

        FormCard.FormButtonDelegate {
            text: root.i18n.dlg_add_by_mac
            icon.name: "list-add-symbolic"
            enabled: manualMacField.text.trim().length >= 17
            onClicked: {
                if (backend.addSavedDevice(manualMacField.text, manualAliasField.text, manualAliasField.text)) {
                    manualMacField.text = ""
                    manualAliasField.text = ""
                    addDeviceDialog.close()
                }
            }
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormButtonDelegate {
            text: root.i18n.dlg_pair_kde
            icon.name: "preferences-system-bluetooth"
            onClicked: backend.openSystemBluetoothSettings()
        }
    }

    // ---- Dialog: Rename Earbuds ----
    FormCard.FormCardDialog {
        id: renameDialog
        property string targetMac: ""
        title: root.i18n.dlg_rename_title
        width: Math.min(root.width - Kirigami.Units.gridUnit * 3, Kirigami.Units.gridUnit * 26)
        standardButtons: Controls.Dialog.Save | Controls.Dialog.Cancel

        onAccepted: {
            if (targetMac.length > 0) {
                backend.renameSavedDevice(targetMac, renameField.text)
            }
        }

        FormCard.FormTextFieldDelegate {
            id: renameField
            label: root.i18n.dlg_rename_label
            placeholderText: root.i18n.dlg_rename_placeholder
        }
    }

    // ---- Dialog: Find Earbuds Safety Warning ----
    FormCard.FormCardDialog {
        id: findConfirmDialog
        title: root.i18n.find_warn_title
        width: Math.min(root.width - Kirigami.Units.gridUnit * 3, Kirigami.Units.gridUnit * 26)
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.Cancel

        onAccepted: backend.startFindEarbuds()

        FormCard.FormTextDelegate {
            text: root.i18n.find_warn_heading
            description: root.i18n.find_warn_body
            leadingPadding: Kirigami.Units.largeSpacing
            leading: Kirigami.Icon {
                source: "dialog-warning"
                implicitWidth: Kirigami.Units.iconSizes.medium
                implicitHeight: Kirigami.Units.iconSizes.medium
            }
        }
    }

    // ---- Dialog: Diagnostic Report for Adding New Earbuds ----
    FormCard.FormCardDialog {
        id: probeDialog
        title: root.i18n.dlg_probe_title
        width: Math.min(root.width - Kirigami.Units.gridUnit * 3, Kirigami.Units.gridUnit * 32)
        standardButtons: Controls.Dialog.Close

        FormCard.FormSectionText {
            text: root.i18n.dlg_probe_hint
        }

        Controls.ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 12
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing

            Controls.TextArea {
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                font.family: "monospace"
                font.pointSize: 9
                text: backend.probeReport
            }
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormButtonDelegate {
            enabled: !backend.probeBusy && backend.probeReport.length > 0
            text: root.i18n.dlg_probe_copy
            icon.name: "edit-copy-symbolic"
            onClicked: {
                backend.copyReportToClipboard()
                probeDialog.close()
            }
        }
    }
}
