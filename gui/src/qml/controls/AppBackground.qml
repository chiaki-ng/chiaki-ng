import QtQuick
import QtQuick.Shapes

// Static ambient backdrop: deep gradient with two soft color glows.
// Rendered once; nothing here animates, so it costs nothing per frame.
Item {
    id: backdrop

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0D1019" }
            GradientStop { position: 0.55; color: Theme.bg }
            GradientStop { position: 1.0; color: Theme.bgDeep }
        }
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.GeometryRenderer

        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: backdrop.width * 0.12
                centerY: -backdrop.height * 0.05
                centerRadius: Math.max(backdrop.width, backdrop.height) * 0.62
                focalX: centerX
                focalY: centerY
                GradientStop { position: 0.0; color: "#384D8DFF" }
                GradientStop { position: 0.45; color: "#124D8DFF" }
                GradientStop { position: 1.0; color: "#004D8DFF" }
            }
            startX: 0; startY: 0
            PathLine { x: backdrop.width; y: 0 }
            PathLine { x: backdrop.width; y: backdrop.height }
            PathLine { x: 0; y: backdrop.height }
            PathLine { x: 0; y: 0 }
        }

        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: backdrop.width * 0.95
                centerY: backdrop.height * 1.05
                centerRadius: Math.max(backdrop.width, backdrop.height) * 0.55
                focalX: centerX
                focalY: centerY
                GradientStop { position: 0.0; color: "#2E8B5CF6" }
                GradientStop { position: 0.5; color: "#0D8B5CF6" }
                GradientStop { position: 1.0; color: "#008B5CF6" }
            }
            startX: 0; startY: 0
            PathLine { x: backdrop.width; y: 0 }
            PathLine { x: backdrop.width; y: backdrop.height }
            PathLine { x: 0; y: backdrop.height }
            PathLine { x: 0; y: 0 }
        }
    }
}
