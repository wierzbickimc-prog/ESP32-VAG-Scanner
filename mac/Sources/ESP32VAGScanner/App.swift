import AppKit
import SwiftUI

@main
struct ESP32VAGScannerApp: App {
    @StateObject private var model = AppModel()

    init() {
        // Launched from `swift run`, the process needs this to get a Dock icon and key focus.
        NSApplication.shared.setActivationPolicy(.regular)
    }

    var body: some Scene {
        WindowGroup("ESP32 VAG Scanner") {
            RootView()
                .environmentObject(model)
                .frame(minWidth: 980, minHeight: 640)
                .preferredColorScheme(.dark)
                .onAppear { NSApp.activate(ignoringOtherApps: true) }
        }
    }
}

@MainActor
final class AppModel: ObservableObject {
    @Published var ports: [String] = []
    @Published var port = ""
    @Published var baud = 9600
    @Published var modules: [Module] = []
    @Published var selection: Module?
    @Published var scanning = false
    @Published var scanned = false
    @Published var scanStatus = ""
    @Published var busy = false
    @Published var error: String?
    /// Fault codes read so far this scan, by controller address.
    @Published var faults: [Int: [FaultCode]] = [:]
    @Published private(set) var car: Car

    let kline = KLine()
    private var scanStop: StopFlag?
    private var carChosenByHand = false

    /// `--demo[=eurovan|harlequin|golf]`: start on a simulated car and scan it;
    /// the engine opens too unless `--overview` is given.
    let autoDemo = CommandLine.arguments.contains { $0.hasPrefix("--demo") }
    private let demoCar = CommandLine.arguments.first { $0.hasPrefix("--demo=") }?.dropFirst(7) ?? "eurovan"
    private let stayOnOverview = CommandLine.arguments.contains("--overview")

    init() {
        car = Car(rawValue: UserDefaults.standard.integer(forKey: "car")) ?? .unknown
        FaultTable.load()
        Labels.load()
        refreshPorts()
        if autoDemo {
            port = "demo:\(demoCar)"
            scan(all: false)
        }
    }

    var totalFaults: Int { faults.values.reduce(0) { $0 + $1.count } }

    func startDemo(_ port: String) {
        self.port = port
        scan(all: false)
    }

    func refreshPorts() {
        ports = KLine.availablePorts()
        if !ports.contains(port) { port = ports.first ?? "" }
    }

    func choose(_ car: Car) {
        carChosenByHand = true
        setCar(car)
    }

    private func setCar(_ car: Car) {
        self.car = car
        UserDefaults.standard.set(car.rawValue, forKey: "car")
    }

    func scan(all: Bool) {
        let stop = StopFlag()
        scanStop = stop
        scanning = true
        busy = true
        error = nil
        modules = []
        selection = nil
        faults = [:]
        carChosenByHand = false
        let addresses = all ? Array(1...0x7F) : commonAddresses
        Task {
            do {
                modules = try await kline.scan(port: port, baud: baud, addresses: addresses, stop: stop) { address in
                    Task { @MainActor in self.scanStatus = String(format: "Probing %02X…", address) }
                }
                scanStatus = modules.isEmpty
                    ? "No controllers answered. Is the ignition on?"
                    : "\(modules.count) controller\(modules.count == 1 ? "" : "s") found"
                if !modules.isEmpty && !carChosenByHand {
                    setCar(Car.identify(modules))
                }
                if KLine.isDemo(port) {
                    // demos show fault counts straight away
                    for module in modules where module.supported {
                        scanStatus = "Reading faults: \(module.name)…"
                        faults[module.address] = try? await kline.readFaults(port: port, baud: baud,
                                                                              address: module.address)
                    }
                    scanStatus = "\(modules.count) controllers found"
                }
                if autoDemo && !stayOnOverview { selection = modules.first }
            } catch {
                self.error = error.localizedDescription
                scanStatus = ""
            }
            scanned = true
            scanning = false
            busy = false
        }
    }

    func stopScan() { scanStop?.stop() }
}

struct RootView: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        NavigationSplitView {
            Sidebar()
                .navigationSplitViewColumnWidth(min: 280, ideal: 300)
        } detail: {
            Group {
                if let module = model.selection {
                    ModuleView(module: module).id(module.address)
                } else {
                    OverviewView()
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Color.appBackground)
        }
    }
}

struct Sidebar: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        List(selection: $model.selection) {
            Section {
                Button {
                    model.selection = nil
                } label: {
                    Label("Overview", systemImage: "car.side")
                }
                .buttonStyle(.plain)
                .padding(.vertical, 4)
            }
            if !model.modules.isEmpty {
                Section("Controllers") {
                    ForEach(model.modules) { module in
                        NavigationLink(value: module) { SidebarRow(module: module) }
                            .disabled(!module.supported)
                    }
                }
            }
        }
        .listStyle(.sidebar)
        .safeAreaInset(edge: .top) { ConnectionPanel() }
    }
}

struct SidebarRow: View {
    let module: Module
    @EnvironmentObject var model: AppModel

    var body: some View {
        HStack(spacing: 10) {
            AddressBadge(address: module.hexAddress, size: 32)
            VStack(alignment: .leading, spacing: 2) {
                Text(module.name).font(.body.weight(.medium))
                Text(module.supported ? (module.identLines.first ?? "") : "KWP\(module.protocolVersion) · not supported")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
            Spacer(minLength: 0)
            if let count = model.faults[module.address]?.count, count > 0 {
                Text("\(count)")
                    .font(.caption.weight(.bold))
                    .foregroundStyle(.white)
                    .padding(.horizontal, 7)
                    .padding(.vertical, 2)
                    .background(Capsule().fill(Color.fault))
            }
        }
        .padding(.vertical, 3)
        .opacity(module.supported ? 1 : 0.5)
    }
}

struct ConnectionPanel: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Picker("Cable", selection: $model.port) {
                    ForEach(model.ports, id: \.self) { Text(KLine.displayName($0)).tag($0) }
                }
                .labelsHidden()
                Button { model.refreshPorts() } label: { Image(systemName: "arrow.clockwise") }
                    .help("Look for USB cables again")
            }
            Picker("Baud", selection: $model.baud) {
                Text("9600").tag(9600)
                Text("10400").tag(10400)
                Text("4800").tag(4800)
            }
            .pickerStyle(.segmented)
            .labelsHidden()

            if model.scanning {
                Button(role: .cancel) { model.stopScan() } label: {
                    HStack {
                        ProgressView().controlSize(.small)
                        Text("Stop")
                    }
                    .frame(maxWidth: .infinity)
                }
                .controlSize(.large)
            } else {
                Button { model.scan(all: false) } label: {
                    Label("Scan car", systemImage: "dot.radiowaves.left.and.right").frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
                .keyboardShortcut("r")
                .disabled(model.port.isEmpty || model.busy)
                .contextMenu {
                    Button("Full scan (01–7F, several minutes)") { model.scan(all: true) }
                }
            }
            if !model.scanStatus.isEmpty {
                Text(model.scanStatus).font(.caption).foregroundStyle(.secondary)
            }
            if let error = model.error {
                Text(error).font(.caption).foregroundStyle(Color.fault)
            }
        }
        .padding(14)
    }
}
