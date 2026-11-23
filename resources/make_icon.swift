// Draws the JamTabaYUP icon: a beat ring around the interval in progress.
// swiftc resources/make_icon.swift -o /tmp/make_icon && /tmp/make_icon resources/icon.png
import AppKit

let size: CGFloat = 1024
let beats = 8
let litBeats = 5

let image = NSImage(size: NSSize(width: size, height: size))
image.lockFocus()
let ctx = NSGraphicsContext.current!.cgContext

// Background: a rounded square in the app's dark grey with a soft top light
let inset: CGFloat = 100
let tile = CGRect(x: inset, y: inset, width: size - 2 * inset, height: size - 2 * inset)
let tilePath = CGPath(roundedRect: tile, cornerWidth: 185, cornerHeight: 185, transform: nil)
ctx.addPath(tilePath)
ctx.clip()
let colors = [NSColor(srgbRed: 0.17, green: 0.19, blue: 0.24, alpha: 1).cgColor,
              NSColor(srgbRed: 0.08, green: 0.09, blue: 0.12, alpha: 1).cgColor] as CFArray
let gradient = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB), colors: colors, locations: [0, 1])!
ctx.drawLinearGradient(gradient, start: CGPoint(x: 0, y: tile.maxY), end: CGPoint(x: 0, y: tile.minY), options: [])

// The beat ring: eight arcs, the first five lit in teal, the current one brightest
let centre = CGPoint(x: size / 2, y: size / 2)
let radius: CGFloat = 255
let gap: CGFloat = 0.24
ctx.setLineWidth(74)
ctx.setLineCap(.round)
for i in 0..<beats {
    let start = CGFloat.pi / 2 - CGFloat(i) * 2 * .pi / CGFloat(beats) - gap
    let end = start - 2 * .pi / CGFloat(beats) + 2 * gap
    let color: NSColor
    if i == litBeats - 1 { color = NSColor(srgbRed: 0.40, green: 0.85, blue: 0.77, alpha: 1) }
    else if i < litBeats - 1 { color = NSColor(srgbRed: 0.25, green: 0.72, blue: 0.66, alpha: 0.75) }
    else { color = NSColor(srgbRed: 0.20, green: 0.23, blue: 0.29, alpha: 1) }
    ctx.setStrokeColor(color.cgColor)
    ctx.addArc(center: centre, radius: radius, startAngle: start, endAngle: end, clockwise: true)
    ctx.strokePath()
}

// Transmit light in the middle
ctx.setFillColor(NSColor(srgbRed: 0.88, green: 0.32, blue: 0.31, alpha: 1).cgColor)
ctx.fillEllipse(in: CGRect(x: centre.x - 78, y: centre.y - 78, width: 156, height: 156))
ctx.setFillColor(NSColor(srgbRed: 1, green: 0.55, blue: 0.53, alpha: 0.55).cgColor)
ctx.fillEllipse(in: CGRect(x: centre.x - 38, y: centre.y + 6, width: 52, height: 40))

image.unlockFocus()
let rep = NSBitmapImageRep(data: image.tiffRepresentation!)!
let png = rep.representation(using: .png, properties: [:])!
try! png.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
