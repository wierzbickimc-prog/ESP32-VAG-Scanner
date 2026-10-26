import AppKit
import SwiftUI

/// The same palette as the P4 (esp32/main/gui.c).
extension Color {
    static let appBackground = Color(hex: 0x0D1014)
    static let surface = Color(hex: 0x161B22)
    static let surfaceRaised = Color(hex: 0x222A35)
    static let textPrimary = Color(hex: 0xE8ECF1)
    static let textMuted = Color(hex: 0x8B95A3)
    static let accent = Color(hex: 0x3D8BFF)
    static let ok = Color(hex: 0x3FB950)
    static let warn = Color(hex: 0xF5A524)
    static let fault = Color(hex: 0xFF5A5F)

    init(hex: UInt32) {
        self.init(red: Double((hex >> 16) & 0xFF) / 255,
                  green: Double((hex >> 8) & 0xFF) / 255,
                  blue: Double(hex & 0xFF) / 255)
    }
}

struct CardBackground: ViewModifier {
    var color: Color = .surface
    func body(content: Content) -> some View {
        content
            .padding(18)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(RoundedRectangle(cornerRadius: 16, style: .continuous).fill(color))
    }
}

extension View {
    func card(_ color: Color = .surface) -> some View { modifier(CardBackground(color: color)) }
}

struct Chip: View {
    let text: String
    var color: Color = .textMuted
    var systemImage: String?

    var body: some View {
        HStack(spacing: 6) {
            if let systemImage { Image(systemName: systemImage) }
            Text(text)
        }
        .font(.callout.weight(.medium))
        .foregroundStyle(color)
        .padding(.horizontal, 10)
        .padding(.vertical, 5)
        .background(Capsule().fill(color.opacity(0.15)))
    }
}

/// The detected car's photo, melting into the page at the bottom.
struct HeroBanner<Overlay: View>: View {
    let car: Car
    let height: CGFloat
    @ViewBuilder var overlay: Overlay

    var body: some View {
        ZStack(alignment: .bottomLeading) {
            // The photo is an overlay on a flexible colour so its 1800px width
            // never becomes the view's ideal width (which squeezes the sidebar).
            Color.appBackground
                .overlay(alignment: car == .unknown ? .top : .trailing) {
                    if let image = car.heroImage {
                        Image(nsImage: image)
                            .resizable()
                            // a car photo fills; the Mk1-Mk8 timeline must show every generation
                            .aspectRatio(contentMode: car == .unknown ? .fit : .fill)
                            .id(car)
                            .transition(.opacity)
                    }
                }
                .clipped()
            LinearGradient(colors: [.clear, .appBackground], startPoint: .center, endPoint: .bottom)
            if car != .unknown {
                LinearGradient(colors: [.appBackground.opacity(0.9), .clear], startPoint: .leading, endPoint: .center)
            }
            overlay.padding(28)
        }
        .frame(minWidth: 0, maxWidth: .infinity)
        .frame(height: height)
        .clipped()
        .animation(.easeInOut(duration: 0.45), value: car)
    }
}

/// Address badge used in lists and cards.
struct AddressBadge: View {
    let address: String
    var size: CGFloat = 44

    var body: some View {
        Text(address)
            .font(.system(size: size * 0.42, weight: .semibold, design: .rounded))
            .foregroundStyle(Color.accent)
            .frame(width: size, height: size)
            .background(RoundedRectangle(cornerRadius: size * 0.27, style: .continuous).fill(Color.accent.opacity(0.18)))
    }
}
