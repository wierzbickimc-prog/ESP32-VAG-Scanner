import SwiftUI

/// The car at a glance: hero photo, status, and a card per controller.
struct OverviewView: View {
    @EnvironmentObject var model: AppModel
    @State private var showCredits = false

    private var subtitle: String {
        if model.car == .unknown && model.scanned && !model.modules.isEmpty {
            return "Not one of your cars · choose one from the menu"
        }
        return model.car.detail
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 28) {
                HeroBanner(car: model.car, height: 380) {
                    VStack(alignment: .leading, spacing: 10) {
                        Text("ESP32 VAG SCANNER")
                            .font(.caption.weight(.semibold))
                            .tracking(3)
                            .foregroundStyle(Color.textMuted)
                        HStack(alignment: .firstTextBaseline, spacing: 12) {
                            Text(model.car.name)
                                .font(.system(size: 46, weight: .bold))
                                .foregroundStyle(Color.textPrimary)
                            carMenu
                        }
                        Text(subtitle).font(.title3).foregroundStyle(Color.textMuted)
                        HStack(spacing: 8) {
                            connectionChip
                            if !model.modules.isEmpty {
                                Chip(text: "\(model.modules.count) controllers", systemImage: "cpu")
                            }
                            if model.totalFaults > 0 {
                                Chip(text: "\(model.totalFaults) faults", color: .fault,
                                     systemImage: "exclamationmark.triangle.fill")
                            }
                        }
                        .padding(.top, 4)
                    }
                }

                Group {
                    if model.modules.isEmpty {
                        emptyState
                    } else {
                        controllers
                    }
                }
                .padding(.horizontal, 28)

                Button("Photo credits") { showCredits = true }
                    .buttonStyle(.link)
                    .font(.caption)
                    .padding(.horizontal, 28)
                    .padding(.bottom, 24)
            }
        }
        .background(Color.appBackground)
        .sheet(isPresented: $showCredits) { CreditsSheet() }
    }

    private var carMenu: some View {
        Menu {
            ForEach(Car.allCases) { car in
                Button(car == .unknown ? "Other car" : car.name) { model.choose(car) }
            }
        } label: {
            Image(systemName: "chevron.down.circle.fill").font(.title2)
        }
        .menuStyle(.borderlessButton)
        .menuIndicator(.hidden)
        .fixedSize()
        .help("Choose the car by hand")
    }

    private var connectionChip: some View {
        if KLine.isDemo(model.port) {
            return Chip(text: "Demo car", color: .warn, systemImage: "play.circle.fill")
        } else if model.port.isEmpty {
            return Chip(text: "No cable", systemImage: "cable.connector.slash")
        }
        return Chip(text: "KKL cable", color: .ok, systemImage: "cable.connector")
    }

    private var emptyState: some View {
        VStack(spacing: 14) {
            Image(systemName: "dot.radiowaves.left.and.right")
                .font(.system(size: 40))
                .foregroundStyle(Color.accent)
            Text(model.scanning ? model.scanStatus : "Ignition on, then Scan car")
                .font(.title3.weight(.semibold))
            Text("No car handy? Try a demo car: simulated fault codes and live measuring blocks.")
                .foregroundStyle(Color.textMuted)
            HStack(spacing: 10) {
                Button("EuroVan") { model.startDemo("demo:eurovan") }
                Button("Golf Harlequin") { model.startDemo("demo:harlequin") }
                Button("Other VW") { model.startDemo("demo:golf") }
            }
            .controlSize(.large)
            .disabled(model.scanning)
            .padding(.top, 4)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 48)
        .background(RoundedRectangle(cornerRadius: 20, style: .continuous).fill(Color.surface))
    }

    private var controllers: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("CONTROLLERS")
                .font(.caption.weight(.semibold))
                .tracking(3)
                .foregroundStyle(Color.textMuted)
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 280), spacing: 14)], spacing: 14) {
                ForEach(model.modules) { module in
                    ModuleCard(module: module)
                        .onTapGesture { if module.supported { model.selection = module } }
                }
            }
        }
    }
}

struct ModuleCard: View {
    let module: Module
    @EnvironmentObject var model: AppModel
    @State private var hover = false

    var body: some View {
        HStack(alignment: .top, spacing: 14) {
            AddressBadge(address: module.hexAddress)
            VStack(alignment: .leading, spacing: 4) {
                Text(module.name).font(.title3.weight(.semibold)).foregroundStyle(Color.textPrimary)
                Text(module.supported ? (module.identLines.first ?? "") : "KWP\(module.protocolVersion)")
                    .font(.callout)
                    .foregroundStyle(Color.textMuted)
                    .lineLimit(1)
                status.padding(.top, 6)
            }
            Spacer(minLength: 0)
        }
        .card(hover && module.supported ? .surfaceRaised : .surface)
        .opacity(module.supported ? 1 : 0.55)
        .onHover { hover = $0 }
        .animation(.easeOut(duration: 0.15), value: hover)
    }

    @ViewBuilder private var status: some View {
        if !module.supported {
            Text("Not supported").font(.callout).foregroundStyle(Color.textMuted)
        } else if let count = model.faults[module.address]?.count {
            if count == 0 {
                Label("No faults", systemImage: "checkmark.circle.fill").foregroundStyle(Color.ok)
            } else {
                Label("\(count) fault\(count == 1 ? "" : "s")", systemImage: "exclamationmark.triangle.fill")
                    .foregroundStyle(Color.fault)
            }
        } else {
            Text("Open to read faults").font(.callout).foregroundStyle(Color.textMuted)
        }
    }
}

struct CreditsSheet: View {
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Photo credits").font(.title2.weight(.bold))
            Text("Photos from Wikimedia Commons, used under their licences (cropped and resized).")
                .foregroundStyle(.secondary)
            ForEach(Credits.lines, id: \.self) { Text($0).textSelection(.enabled) }
            HStack {
                Spacer()
                Button("Done") { dismiss() }.keyboardShortcut(.defaultAction)
            }
        }
        .padding(24)
        .frame(width: 560)
    }
}
