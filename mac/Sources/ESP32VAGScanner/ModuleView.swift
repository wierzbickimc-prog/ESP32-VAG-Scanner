import KWCore
import SwiftUI

struct ModuleView: View {
    let module: Module
    @EnvironmentObject var model: AppModel

    private enum Tab { case faults, live }
    @State private var tab = Tab.faults

    private var faults: [FaultCode]? { model.faults[module.address] }
    @State private var faultStatus = ""
    @State private var confirmClear = false
    @State private var selectedFault: FaultCode?

    @State private var group = 1
    @State private var values: [MeasuredValue] = []
    /// The group changed while live: restart on the new group once the bus is free.
    @State private var restartLive = false
    @State private var groupStatus = ""
    @State private var liveStop: StopFlag?

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 22) {
                HeroBanner(car: model.car, height: 230) {
                    HStack(alignment: .bottom, spacing: 16) {
                        AddressBadge(address: module.hexAddress, size: 56)
                        VStack(alignment: .leading, spacing: 4) {
                            Text(module.name)
                                .font(.system(size: 36, weight: .bold))
                                .foregroundStyle(Color.textPrimary)
                            Text(verbatim: "Address \(module.hexAddress) · KW\(module.protocolVersion)")
                                .foregroundStyle(Color.textMuted)
                        }
                    }
                }

                VStack(alignment: .leading, spacing: 22) {
                    VStack(alignment: .leading, spacing: 4) {
                        ForEach(module.identLines, id: \.self) { line in
                            Text(line).font(.system(.callout, design: .monospaced))
                        }
                    }
                    .foregroundStyle(Color.textMuted)
                    .textSelection(.enabled)
                    .card()

                    Picker("", selection: $tab) {
                        Label("Fault codes", systemImage: "exclamationmark.triangle").tag(Tab.faults)
                        Label("Live data", systemImage: "gauge.with.dots.needle.33percent").tag(Tab.live)
                    }
                    .pickerStyle(.segmented)
                    .labelsHidden()
                    .frame(maxWidth: 360)

                    switch tab {
                    case .faults: faultSection
                    case .live: liveSection
                    }
                }
                .padding(.horizontal, 28)
                .padding(.bottom, 28)
            }
        }
        .background(Color.appBackground)
        .navigationTitle(module.name)
        .onDisappear {
            restartLive = false
            liveStop?.stop()
        }
        .onChange(of: model.busy) { busy in
            if !busy && restartLive {
                restartLive = false
                readGroup(once: false)
            }
        }
        .task {
            guard model.autoDemo else { return }
            while model.busy { try? await Task.sleep(nanoseconds: 100_000_000) }
            if CommandLine.arguments.contains("--live") {
                tab = .live
                readGroup(once: false)
            } else if faults == nil {
                readFaults()
            }
        }
    }

    // MARK: Faults

    private var faultSection: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(spacing: 10) {
                Button { readFaults() } label: {
                    Label("Read faults", systemImage: "arrow.down.circle")
                }
                .buttonStyle(.borderedProminent)
                Button(role: .destructive) { confirmClear = true } label: {
                    Label("Clear", systemImage: "trash")
                }
                .disabled(faults?.isEmpty ?? true)
                Spacer()
                Text(faultStatus).font(.callout).foregroundStyle(Color.textMuted)
            }
            .controlSize(.large)
            .disabled(model.busy)

            if let faults {
                if faults.isEmpty {
                    VStack(spacing: 10) {
                        Image(systemName: "checkmark.seal.fill").font(.system(size: 44)).foregroundStyle(Color.ok)
                        Text("No fault codes stored").font(.title3.weight(.semibold))
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 36)
                    .background(RoundedRectangle(cornerRadius: 16, style: .continuous).fill(Color.surface))
                } else {
                    ForEach(faults) { fault in
                        Button { selectedFault = fault } label: { FaultCard(fault: fault) }
                            .buttonStyle(.plain)
                    }
                }
            }
        }
        .confirmationDialog("Clear all fault codes on \(module.name)?", isPresented: $confirmClear) {
            Button("Clear fault codes", role: .destructive) { clearFaults() }
        } message: {
            Text("Write the codes down first if you need them. Faults that are still present will come back.")
        }
        .sheet(item: $selectedFault) { FaultDetail(fault: $0) }
    }

    // MARK: Live data

    private var liveSection: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(spacing: 10) {
                Button { stepGroup(-1) } label: { Image(systemName: "minus") }
                Text("Group \(group)")
                    .font(.title3.weight(.semibold).monospacedDigit())
                    .frame(minWidth: 110)
                Button { stepGroup(1) } label: { Image(systemName: "plus") }
                Divider().frame(height: 22)
                Button("Read once") { readGroup(once: true) }
                    .disabled(model.busy)
                if liveStop == nil {
                    Button { readGroup(once: false) } label: { Label("Live", systemImage: "play.fill") }
                        .buttonStyle(.borderedProminent)
                        .disabled(model.busy)
                } else {
                    Button { liveStop?.stop() } label: { Label("Stop", systemImage: "stop.fill") }
                        .buttonStyle(.borderedProminent)
                        .tint(.fault)
                }
                Spacer()
                Text(groupStatus).font(.callout).foregroundStyle(Color.textMuted)
            }
            .controlSize(.large)
            .disabled(liveStop != nil && false)

            if !values.isEmpty && kw_labels_standard(module.ident, Int32(group)) != 0 {
                Label("Labels from VW's standard petrol-engine groups: typical, not confirmed for this ECU",
                      systemImage: "info.circle")
                    .font(.caption)
                    .foregroundStyle(Color.textMuted)
            }
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 220), spacing: 14)], spacing: 14) {
                ForEach(values.indices, id: \.self) { i in
                    VStack(alignment: .leading, spacing: 14) {
                        Text("\(i + 1) · \(values[i].label)")
                            .font(.caption.weight(.semibold))
                            .foregroundStyle(Color.textMuted)
                            .lineLimit(1)
                        Text(values[i].value)
                            .font(.system(size: 30, weight: .semibold, design: .rounded).monospacedDigit())
                            .foregroundStyle(Color.textPrimary)
                            .lineLimit(1)
                            .minimumScaleFactor(0.6)
                    }
                    .card()
                }
            }
        }
    }

    // MARK: Actions

    /// Steps the group: switches a live stream over, or reads the new group once.
    private func stepGroup(_ delta: Int) {
        group = min(255, max(0, group + delta))
        if let liveStop {
            restartLive = true
            liveStop.stop()
        } else if !model.busy {
            readGroup(once: true)
        }
    }

    private func readFaults() {
        perform(status: $faultStatus, label: "Reading…") {
            model.faults[module.address] = try await model.kline.readFaults(port: model.port, baud: model.baud,
                                                                            address: module.address)
            faultStatus = "Read \(Date.now.formatted(date: .omitted, time: .shortened))"
        }
    }

    private func clearFaults() {
        perform(status: $faultStatus, label: "Clearing…") {
            let remaining = try await model.kline.clearFaults(port: model.port, baud: model.baud, address: module.address)
            model.faults[module.address] = remaining
            faultStatus = remaining.isEmpty ? "Cleared" : "Some faults are still present"
        }
    }

    private func readGroup(once: Bool) {
        let stop = StopFlag()
        if !once { liveStop = stop }
        values = []
        perform(status: $groupStatus, label: once ? "Reading…" : "Live") {
            defer { liveStop = nil }
            try await model.kline.readGroup(port: model.port, baud: model.baud, address: module.address,
                                            group: group, once: once, stop: stop) { newValues in
                Task { @MainActor in values = newValues }
            }
            groupStatus = ""
        }
    }

    private func perform(status: Binding<String>, label: String, _ work: @escaping () async throws -> Void) {
        model.busy = true
        status.wrappedValue = label
        Task {
            do {
                try await work()
            } catch {
                status.wrappedValue = error.localizedDescription
            }
            model.busy = false
        }
    }
}

struct FaultCard: View {
    let fault: FaultCode

    var body: some View {
        HStack(spacing: 0) {
            Rectangle()
                .fill(fault.intermittent ? Color.warn : Color.fault)
                .frame(width: 5)
            VStack(alignment: .leading, spacing: 8) {
                HStack(spacing: 10) {
                    Text(fault.code)
                        .font(.system(size: 22, weight: .semibold, design: .monospaced))
                        .foregroundStyle(Color.textPrimary)
                    if let pcode = fault.pcode {
                        Chip(text: pcode, color: .textPrimary)
                    }
                    if fault.intermittent {
                        Chip(text: "Intermittent", color: .warn)
                    }
                    Spacer()
                    Text(fault.statusText).font(.system(.callout, design: .monospaced)).foregroundStyle(Color.textMuted)
                    Link(destination: fault.wikiURL) {
                        Image(systemName: "arrow.up.right.square")
                    }
                    .help("Look up \(fault.code) on the Ross-Tech wiki (not every code has a page)")
                }
                Text(fault.description ?? "No description in the fault table")
                    .foregroundStyle(fault.description == nil ? Color.textMuted : Color.textPrimary)
                if let type = fault.type {
                    Text(type)
                        .font(.callout.weight(.medium))
                        .foregroundStyle(fault.intermittent ? Color.warn : Color.fault)
                }
            }
            .padding(16)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color.surface)
        .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
        .contentShape(Rectangle())
        .help("Show details")
    }
}

/// Everything a KW1281 controller stores about one fault: the code and one status byte.
struct FaultDetail: View {
    let fault: FaultCode
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            HStack(spacing: 10) {
                Text(fault.code)
                    .font(.system(size: 28, weight: .semibold, design: .monospaced))
                    .textSelection(.enabled)
                if let pcode = fault.pcode {
                    Chip(text: pcode, color: .textPrimary)
                }
                Spacer()
                Button("Done") { dismiss() }.keyboardShortcut(.defaultAction)
            }
            Text(fault.description ?? "No description in the fault table")
                .font(.title3)
                .foregroundStyle(fault.description == nil ? Color.textMuted : Color.textPrimary)
                .textSelection(.enabled)
                .fixedSize(horizontal: false, vertical: true)

            Grid(alignment: .leadingFirstTextBaseline, horizontalSpacing: 16, verticalSpacing: 12) {
                GridRow {
                    Text("Fault type").foregroundStyle(Color.textMuted)
                    Text(String(format: "%02d", fault.status & 0x7F) + " · " + (fault.type ?? "None given"))
                }
                GridRow {
                    Text("Occurrence").foregroundStyle(Color.textMuted)
                    Text(fault.intermittent
                         ? "Intermittent: the fault came and went"
                         : "Static: the controller saw it continuously")
                        .foregroundStyle(fault.intermittent ? Color.warn : Color.fault)
                }
                GridRow {
                    Text("Status byte").foregroundStyle(Color.textMuted)
                    Text(String(format: "0x%02X, shown as %@", fault.status, fault.statusText))
                        .font(.system(.body, design: .monospaced))
                }
            }
            .textSelection(.enabled)

            Text("This controller uses KW1281, which stores only the code and its status byte. "
                 + "There is no mileage, occurrence count or freeze frame data to read. "
                 + "Note the code down before clearing.")
                .font(.callout)
                .foregroundStyle(Color.textMuted)
                .fixedSize(horizontal: false, vertical: true)

            Link(destination: fault.wikiURL) {
                Label("Look up \(fault.code) on the Ross-Tech wiki", systemImage: "arrow.up.right.square")
            }
        }
        .padding(28)
        .frame(width: 520)
        .background(Color.surface)
    }
}
