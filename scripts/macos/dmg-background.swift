import AppKit

let width: CGFloat = 660
let height: CGFloat = 400
let lime = NSColor(srgbRed: 0.68, green: 0.78, blue: 0.04, alpha: 1)
let label = CommandLine.arguments.count > 2
    ? "\(CommandLine.arguments[2].uppercased()) BUILD"
    : ""

func render(scale: CGFloat, to url: URL) throws {
    guard let bitmap = NSBitmapImageRep(
        bitmapDataPlanes: nil,
        pixelsWide: Int(width * scale),
        pixelsHigh: Int(height * scale),
        bitsPerSample: 8,
        samplesPerPixel: 4,
        hasAlpha: true,
        isPlanar: false,
        colorSpaceName: .deviceRGB,
        bytesPerRow: 0,
        bitsPerPixel: 0
    ) else {
        throw CocoaError(.fileWriteUnknown)
    }
    bitmap.size = NSSize(width: width, height: height)
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)

    let bounds = NSRect(x: 0, y: 0, width: width, height: height)
    NSGradient(
        starting: NSColor(srgbRed: 0.08, green: 0.115, blue: 0.15, alpha: 1),
        ending: NSColor(srgbRed: 0.055, green: 0.075, blue: 0.10, alpha: 1)
    )?.draw(in: bounds, angle: -90)

    let arrow = NSBezierPath()
    arrow.move(to: NSPoint(x: 262, y: 210))
    arrow.line(to: NSPoint(x: 388, y: 210))
    arrow.lineWidth = 4
    arrow.lineCapStyle = .round
    lime.setStroke()
    arrow.stroke()
    let head = NSBezierPath()
    head.move(to: NSPoint(x: 404, y: 210))
    head.line(to: NSPoint(x: 384, y: 224))
    head.line(to: NSPoint(x: 384, y: 196))
    head.close()
    lime.setFill()
    head.fill()

    let paragraph = NSMutableParagraphStyle()
    paragraph.alignment = .center
    let title = NSAttributedString(
        string: "Drag chiaki-ng to Applications",
        attributes: [
            .font: NSFont.systemFont(ofSize: 17, weight: .semibold),
            .foregroundColor: NSColor(white: 0.92, alpha: 1),
            .paragraphStyle: paragraph,
        ]
    )
    title.draw(in: NSRect(x: 0, y: 64, width: width, height: 26))

    if !label.isEmpty {
        let badge = NSAttributedString(
            string: label,
            attributes: [
                .font: NSFont.systemFont(ofSize: 13, weight: .heavy),
                .foregroundColor: NSColor(srgbRed: 0.055, green: 0.075, blue: 0.10, alpha: 1),
                .kern: 1.2,
            ]
        )
        let badgeSize = badge.size()
        let pill = NSRect(
            x: (width - badgeSize.width - 28) / 2,
            y: 336,
            width: badgeSize.width + 28,
            height: 26
        )
        lime.setFill()
        NSBezierPath(roundedRect: pill, xRadius: 13, yRadius: 13).fill()
        badge.draw(at: NSPoint(x: pill.midX - badgeSize.width / 2, y: pill.midY - badgeSize.height / 2))
    }

    NSGraphicsContext.restoreGraphicsState()
    guard let data = bitmap.representation(using: .png, properties: [:]) else {
        throw CocoaError(.fileWriteUnknown)
    }
    try data.write(to: url)
}

let output = URL(fileURLWithPath: CommandLine.arguments[1])
try render(scale: 1, to: output.appendingPathComponent("dmg-background.png"))
try render(scale: 2, to: output.appendingPathComponent("dmg-background@2x.png"))
