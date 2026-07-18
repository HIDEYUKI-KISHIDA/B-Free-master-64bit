import QtQuick 2.15

/**
 * KPI / stress: continuous scene updates to drive wl_surface.commit + frame callbacks.
 * Use with: QT_QPA_PLATFORM=wayland qmlscene tools/qml_wayland_kpi_bench.qml
 */
Rectangle {
    id: root
    width: 320
    height: 240
    color: "#1a2228"

    property int tick: 0

    Timer {
        interval: 16
        running: true
        repeat: true
        onTriggered: root.tick++
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 12
        text: "B-Free KPI bench  tick=" + root.tick
        color: "#e0e8f0"
        font.pixelSize: 14
    }

    Rectangle {
        id: spinner
        width: 72
        height: 72
        radius: 8
        color: Qt.rgba(0.35 + 0.15 * Math.sin(root.tick * 0.08), 0.45, 0.55, 1)
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 16

        RotationAnimation on rotation {
            from: 0
            to: 360
            duration: 1800
            loops: Animation.Infinite
        }
    }

    Rectangle {
        width: 12
        height: 12
        radius: 6
        color: "#ff8060"
        x: 24 + (parent.width - 48 - 24) * (0.5 + 0.5 * Math.sin(root.tick * 0.12))
        y: parent.height - 36
    }
}
