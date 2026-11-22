import QtQuick

// Original vector motifs inspired by the selected game, drawn only in
// the console browser. No animation or decoration covers the video stream.
Rectangle {
    id: backdrop
    required property string themeKey
    required property var themePalette
    property bool showLabel: true
    color: themePalette.background

    Rectangle {
        anchors.fill: parent
        visible: backdrop.themeKey !== "chiaki"
        gradient: Gradient {
            GradientStop { position: 0; color: backdrop.themePalette.primary }
            GradientStop { position: 1; color: backdrop.themePalette.background }
        }
        opacity: 0.4
    }

    Canvas {
        id: art
        anchors.fill: parent
        opacity: 0.22
        visible: backdrop.themeKey !== "chiaki"
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: backdrop
            function onThemeKeyChanged() { art.requestPaint(); }
            function onThemePaletteChanged() { art.requestPaint(); }
        }
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            ctx.scale(width / 1000, height / 700);
            ctx.strokeStyle = backdrop.themePalette.accent;
            ctx.fillStyle = backdrop.themePalette.accent;
            ctx.lineWidth = 2;
            function line(points) {
                ctx.beginPath(); ctx.moveTo(points[0][0], points[0][1]);
                for (let i = 1; i < points.length; ++i)
                    ctx.lineTo(points[i][0], points[i][1]);
                ctx.stroke();
            }
            function circle(x, y, radius) {
                ctx.beginPath(); ctx.arc(x, y, radius, 0, Math.PI * 2); ctx.stroke();
            }
            switch (backdrop.themeKey) {
            case "spiderman":
                for (let i = 0; i < 16; ++i) {
                    const a = i * Math.PI / 8;
                    line([[840, 200], [840 + Math.cos(a) * 900, 200 + Math.sin(a) * 900]]);
                }
                for (let r = 90; r < 850; r += 85) circle(840, 200, r);
                break;
            case "gow":
                circle(770, 350, 195); circle(770, 350, 210);
                ctx.lineWidth = 8;
                line([[700, 570], [805, 180]]);
                line([[790, 220], [690, 185], [665, 300], [770, 285]]);
                for (let i = 0; i < 12; ++i) {
                    const a = i * Math.PI / 6;
                    line([[770 + 180 * Math.cos(a), 350 + 180 * Math.sin(a)],
                          [770 + 192 * Math.cos(a), 350 + 192 * Math.sin(a)]]);
                }
                break;
            case "horizon":
                circle(780, 220, 90);
                line([[0, 620], [160, 400], [270, 510], [490, 280], [660, 490], [820, 360], [1000, 560]]);
                line([[0, 690], [220, 540], [420, 610], [640, 420], [1000, 650]]);
                for (let y = 540; y < 700; y += 28) line([[0, y], [1000, y]]);
                break;
            case "tlou":
                for (let stem = 0; stem < 5; ++stem) {
                    const x = 650 + stem * 75;
                    line([[x, 700], [x - 40, 110 + stem * 30]]);
                    for (let y = 220; y < 650; y += 70) {
                        line([[x - 30, y + 25], [x - 85, y - 10], [x - 60, y + 35], [x - 30, y + 25]]);
                        line([[x - 25, y + 50], [x + 30, y + 10], [x + 5, y + 60], [x - 25, y + 50]]);
                    }
                }
                break;
            case "ghost":
                circle(805, 235, 105);
                for (let i = 0; i < 8; ++i) {
                    ctx.beginPath(); ctx.moveTo(100, 390 + i * 35);
                    ctx.bezierCurveTo(350, 220 + i * 40, 600, 620, 1050, 350 + i * 35); ctx.stroke();
                }
                break;
            case "astro":
                circle(790, 350, 150); circle(790, 350, 225);
                for (let i = 0; i < 35; ++i) {
                    const x = (i * 163 + 53) % 1000, y = (i * 97 + 81) % 700;
                    line([[x - 5, y], [x + 5, y]]); line([[x, y - 5], [x, y + 5]]);
                }
                ctx.lineWidth = 9;
                line([[750, 335], [750, 365]]); line([[830, 335], [830, 365]]);
                break;
            case "gt7":
                ctx.lineWidth = 10;
                line([[600, 520], [530, 300], [650, 200], [880, 240], [930, 430], [790, 560], [600, 520]]);
                ctx.lineWidth = 2;
                line([[625, 480], [580, 315], [665, 250], [840, 280], [875, 410], [775, 510], [625, 480]]);
                for (let i = 0; i < 7; ++i) line([[560, 500 + i * 10], [610, 485 + i * 10]]);
                break;
            case "ratchet":
                for (let g = 0; g < 3; ++g) {
                    const x = 650 + g * 100, y = 260 + (g % 2) * 170;
                    circle(x, y, 85); circle(x, y, 30);
                    for (let i = 0; i < 12; ++i) {
                        const a = i * Math.PI / 6;
                        line([[x + 85 * Math.cos(a), y + 85 * Math.sin(a)], [x + 105 * Math.cos(a), y + 105 * Math.sin(a)]]);
                    }
                }
                break;
            case "returnal":
                for (let r = 70; r <= 250; r += 45) circle(780, 350, r);
                for (let i = 0; i < 35; ++i) {
                    const x = (i * 139) % 1000, y = (i * 71) % 700;
                    line([[x, y], [x - 12, y + 75]]);
                }
                break;
            case "uncharted":
                circle(790, 350, 160); circle(790, 350, 180);
                line([[790, 120], [835, 350], [790, 580], [745, 350], [790, 120]]);
                line([[560, 350], [790, 305], [1020, 350], [790, 395], [560, 350]]);
                break;
            case "bloodborne":
                for (let i = 0; i < 6; ++i) {
                    const x = 520 + i * 85, y = 270 + (i % 3) * 60;
                    line([[x - 25, 700], [x - 25, y + 70], [x, y], [x + 25, y + 70], [x + 25, 700]]);
                    line([[x, y], [x, y - 55]]); line([[x - 15, y - 35], [x + 15, y - 35]]);
                }
                circle(800, 170, 80);
                break;
            case "demons":
                circle(790, 350, 200);
                line([[790, 130], [815, 210], [800, 420], [780, 420], [765, 210], [790, 130]]);
                ctx.lineWidth = 7;
                line([[710, 420], [870, 420]]); line([[790, 420], [790, 560]]);
                break;
            case "stranding":
                for (let i = 0; i < 14; ++i) {
                    ctx.beginPath(); ctx.moveTo(420, 90 + i * 35);
                    ctx.bezierCurveTo(690, 120, 580, 620, 1040, 200 + i * 30); ctx.stroke();
                }
                for (let i = 0; i < 9; ++i) line([[600 + i * 42, 0], [600 + i * 42, 140 + (i % 3) * 100]]);
                break;
            default:
                for (let i = 0; i < 9; ++i) {
                    ctx.beginPath(); ctx.moveTo(-100, 530 + i * 25);
                    ctx.bezierCurveTo(250, 100, 650, 900, 1100, 240 + i * 25); ctx.stroke();
                }
            }
        }
    }
    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 28
        text: backdrop.themePalette.name
        font.pixelSize: 18
        font.letterSpacing: 2
        color: backdrop.themePalette.accent
        opacity: 0.6
        visible: backdrop.showLabel && backdrop.themeKey !== "chiaki"
    }
}
