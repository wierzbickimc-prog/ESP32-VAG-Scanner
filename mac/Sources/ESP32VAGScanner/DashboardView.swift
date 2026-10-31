import Charts
import KWCore
import SwiftUI

/// Four live measuring-block slots with trends, plus the fuel economy estimate.
struct DashboardView: View {
    @EnvironmentObject var model: AppModel
    @EnvironmentObject var dash: Dashboard

    @State private var window: TimeInterval = 5 * 60
    @State private var editing: Int?
    @State private var calibrating = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                header
                LazyVGrid(columns: [GridItem(.flexible(), spacing: 16), GridItem(.flexible(), spacing: 16)],
                          spacing: 16) {
                    ForEach(dash.slots.indices, id: \.self) { i in
                        DashTile(index: i, window: window, editing: $editing, calibrating: $calibrating)
                    }
                }
                Text("Every controller switch costs a 2–3 second wake-up, so values from a second controller "
                     + "(such as the transmission) update every few seconds; slots on one controller update continuously.")
                    .font(.caption)
                    .foregroundStyle(Color.textMuted)
            }
            .padding(28)
        }
        .background(Color.appBackground)
        .navigationTitle("Dashboard")
        .sheet(item: Binding(get: { editing.map(EditTarget.init) }, set: { editing = $0?.index })) { target in
            SlotEditor(index: target.index)
        }
        .sheet(isPresented: $calibrating) { FuelCalibration() }
        .onDisappear { dash.stopReading() }
        .task {
            guard CommandLine.arguments.contains("--dashboard") else { return }
            while model.busy || !model.scanned { try? await Task.sleep(nanoseconds: 100_000_000) }
            dash.start(model: model)
        }
    }

    private struct EditTarget: Identifiable {
        let index: Int
        var id: Int { index }
    }

    private var header: some View {
        HStack(spacing: 12) {
            VStack(alignment: .leading, spacing: 4) {
                Text("Dashboard").font(.system(size: 30, weight: .bold)).foregroundStyle(Color.textPrimary)
                Text(dash.status.isEmpty ? "Choose a value for each slot, then Start." : dash.status)
                    .foregroundStyle(Color.textMuted)
            }
            Spacer()
            Picker("Trend", selection: $window) {
                Text("1 min").tag(60.0)
                Text("5 min").tag(300.0)
                Text("15 min").tag(900.0)
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .frame(width: 210)
            Picker("Units", selection: $dash.imperial) {
                Text("US").tag(true)
                Text("Metric").tag(false)
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .frame(width: 130)
            if dash.running {
                Button { dash.stopReading() } label: { Label("Stop", systemImage: "stop.fill") }
                    .buttonStyle(.borderedProminent)
                    .tint(.fault)
            } else {
                Button { dash.start(model: model) } label: { Label("Start", systemImage: "play.fill") }
                    .buttonStyle(.borderedProminent)
                    .disabled(model.busy || model.port.isEmpty)
            }
        }
        .controlSize(.large)
    }
}

private struct DashTile: View {
    let index: Int
    let window: TimeInterval
    @Binding var editing: Int?
    @Binding var calibrating: Bool
    @EnvironmentObject var dash: Dashboard

    private var slot: DashSlot { dash.slots[index] }
    private var state: DashSlotState { dash.states[index] }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .firstTextBaseline) {
                VStack(alignment: .leading, spacing: 2) {
                    Text(slot.kind == .empty ? "Empty slot" : slot.name)
                        .font(.headline)
                        .foregroundStyle(Color.textPrimary)
                    Text(subtitle)
                        .font(.caption)
                        .foregroundStyle(Color.textMuted)
                        .lineLimit(1)
                }
                Spacer()
                slotMenu
            }
            switch slot.kind {
            case .empty:
                Button { editing = index } label: {
                    Label("Choose a value", systemImage: "plus.circle").frame(maxWidth: .infinity, minHeight: 200)
                }
                .buttonStyle(.plain)
                .foregroundStyle(Color.textMuted)
            case .sensor:
                sensorBody
            case .fuel:
                fuelBody
            }
            if let error = state.error {
                Label(error, systemImage: "exclamationmark.triangle")
                    .font(.caption)
                    .foregroundStyle(Color.warn)
            } else if let note = slot.note {
                Label(note, systemImage: "info.circle")
                    .font(.caption)
                    .foregroundStyle(Color.textMuted)
                    .lineLimit(2)
            }
        }
        .card()
    }

    private var subtitle: String {
        if let label = state.label, slot.kind == .sensor, label != slot.name {
            return "\(slot.source) · \(label)"
        }
        return slot.source
    }

    private var slotMenu: some View {
        Menu {
            Section("EuroVan presets") {
                ForEach(DashSlot.presets.indices, id: \.self) { i in
                    Button(DashSlot.presets[i].slot.name) { dash.set(DashSlot.presets[i].slot, at: index) }
                }
            }
            Button("Custom…") { editing = index }
            Divider()
            Button("Clear slot") { dash.set(.empty, at: index) }
        } label: {
            Image(systemName: "slider.horizontal.3")
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
        .help("Choose what this slot shows")
    }

    // MARK: Sensor

    private var sensorBody: some View {
        let points = shown(state.history)
        return VStack(alignment: .leading, spacing: 8) {
            if let value = state.value {
                let d = dash.display(value, unit: state.unit)
                BigValue(value: format(d.value, unit: d.unit), unit: d.unit)
            } else {
                BigValue(value: state.text ?? "––", unit: "")
            }
            Trend(points: points, unit: dash.display(0, unit: state.unit).unit, window: window)
            if let lo = points.map(\.value).min(), let hi = points.map(\.value).max() {
                let u = dash.display(0, unit: state.unit).unit
                Text("Low \(format(lo, unit: u)) · High \(format(hi, unit: u)) \(u)")
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(Color.textMuted)
            }
        }
    }

    // MARK: Fuel

    private var fuelBody: some View {
        let instant = kw_fuel_instant([dash.fuel])
        let average = kw_fuel_average([dash.fuel])
        let points = state.history.filter { $0.time > Date().addingTimeInterval(-window) }
            .map { DashSample(time: $0.time, value: dash.economy($0.value)) }
        return VStack(alignment: .leading, spacing: 8) {
            if instant > 0 {
                BigValue(value: String(format: "%.1f", dash.economy(instant)), unit: dash.economyUnit)
            } else if dash.fuel.have_maf != 0 && dash.fuel.have_speed != 0 {
                // standing still: no distance, so 0 mpg (l/100km has no finite value)
                BigValue(value: dash.imperial ? "0.0" : "––", unit: "\(dash.economyUnit) · stopped")
            } else {
                BigValue(value: "––", unit: dash.economyUnit)
            }
            Trend(points: points, unit: dash.economyUnit, window: window)
            let dist = dash.display(dash.fuel.trip_km, unit: "km")
            let used = dash.display(dash.fuel.trip_l, unit: "l")
            HStack(spacing: 8) {
                Text("Trip \(average > 0 ? String(format: "%.1f %@", dash.economy(average), dash.economyUnit) : "––")"
                     + String(format: " · %.1f %@ · %.2f %@", dist.value, dist.unit, used.value, used.unit))
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(Color.textMuted)
                Spacer()
                Button("Reset trip") { dash.resetTrip() }
                Button("Calibrate…") { calibrating = true }
            }
            .controlSize(.small)
            Text(dash.fuel.factor == 1
                 ? "Estimated from air mass and speed. Calibrate after a fill-up for accuracy."
                 : String(format: "Calibrated: × %.3f from a fill-up.", dash.fuel.factor))
                .font(.caption)
                .foregroundStyle(Color.textMuted)
        }
    }

    // MARK: Helpers

    private func shown(_ history: [DashSample]) -> [DashSample] {
        let start = Date().addingTimeInterval(-window)
        return history.filter { $0.time > start }
            .map { DashSample(time: $0.time, value: dash.display($0.value, unit: state.unit).value) }
    }

    private func format(_ v: Double, unit: String) -> String {
        switch unit {
        case "rpm", "km", "mi": return String(format: "%.0f", v)
        case "V", "bar", "ms", "g/s", "Ω", "s": return String(format: "%.2f", v)
        default: return String(format: "%.1f", v)
        }
    }
}

private struct BigValue: View {
    let value: String
    let unit: String

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 6) {
            Text(value)
                .font(.system(size: 44, weight: .semibold, design: .rounded).monospacedDigit())
                .foregroundStyle(Color.textPrimary)
                .lineLimit(1)
                .minimumScaleFactor(0.5)
            Text(unit).font(.title3).foregroundStyle(Color.textMuted)
        }
    }
}

/// One series over the chosen window: a 2px line, quiet axes, a hover readout.
private struct Trend: View {
    let points: [DashSample]
    let unit: String
    let window: TimeInterval
    @State private var hover: DashSample?

    /// The data's range, widened so sensor noise doesn't fill the chart (0.1 V steps look like a seizure).
    private var yDomain: ClosedRange<Double> {
        let values = points.map(\.value)
        guard let lo = values.min(), let hi = values.max() else { return 0...1 }
        let minimum: Double
        switch unit {
        case "V": minimum = 2
        case "°C", "°F": minimum = 20
        case "mpg", "l/100km": minimum = 10
        case "rpm": minimum = 500
        default: minimum = max(abs(hi) * 0.1, 1)
        }
        let span = max(hi - lo, minimum) * 1.1
        let mid = (lo + hi) / 2
        return (mid - span / 2)...(mid + span / 2)
    }

    var body: some View {
        Chart {
            ForEach(points) { p in
                LineMark(x: .value("Time", p.time), y: .value(unit, p.value))
                    .lineStyle(StrokeStyle(lineWidth: 2, lineCap: .round, lineJoin: .round))
                    .foregroundStyle(Color.accent)
                    .interpolationMethod(.monotone)
            }
            if let hover {
                RuleMark(x: .value("Time", hover.time))
                    .foregroundStyle(Color.textMuted.opacity(0.5))
                    .lineStyle(StrokeStyle(lineWidth: 1))
                PointMark(x: .value("Time", hover.time), y: .value(unit, hover.value))
                    .symbolSize(70)
                    .foregroundStyle(Color.accent)
                    .annotation(position: .top, alignment: .center, spacing: 6) {
                        Text(String(format: "%.1f %@ · %@", hover.value, unit,
                                    hover.time.formatted(date: .omitted, time: .standard)))
                            .font(.caption.monospacedDigit())
                            .foregroundStyle(Color.textPrimary)
                            .padding(.horizontal, 6)
                            .padding(.vertical, 3)
                            .background(RoundedRectangle(cornerRadius: 6).fill(Color.surfaceRaised))
                    }
            }
        }
        .chartXScale(domain: Date().addingTimeInterval(-window)...Date())
        .chartYScale(domain: yDomain)
        .chartXAxis {
            AxisMarks(values: .automatic(desiredCount: 4)) { _ in
                AxisGridLine().foregroundStyle(Color.textMuted.opacity(0.15))
                AxisValueLabel(format: .dateTime.hour().minute()).foregroundStyle(Color.textMuted)
            }
        }
        .chartYAxis {
            AxisMarks(position: .trailing, values: .automatic(desiredCount: 3)) { _ in
                AxisGridLine().foregroundStyle(Color.textMuted.opacity(0.15))
                AxisValueLabel().foregroundStyle(Color.textMuted)
            }
        }
        .chartOverlay { proxy in
            GeometryReader { geo in
                Rectangle().fill(.clear).contentShape(Rectangle())
                    .onContinuousHover { phase in
                        guard case .active(let location) = phase,
                              let plot = Optional(geo[proxy.plotAreaFrame]),
                              let time: Date = proxy.value(atX: location.x - plot.origin.x) else {
                            hover = nil
                            return
                        }
                        hover = points.min { abs($0.time.timeIntervalSince(time)) < abs($1.time.timeIntervalSince(time)) }
                    }
            }
        }
        .frame(height: 140)
        .overlay {
            if points.isEmpty {
                Text("Trend appears once values arrive").font(.caption).foregroundStyle(Color.textMuted)
            }
        }
    }
}

/// Picks any controller / group / field for a slot.
private struct SlotEditor: View {
    let index: Int
    @EnvironmentObject var dash: Dashboard
    @Environment(\.dismiss) private var dismiss
    @State private var name = ""
    @State private var address = "01"
    @State private var group = 1
    @State private var field = 1

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Slot \(index + 1)").font(.title2.weight(.semibold))
            Text("Any field of any measuring group. To find one, open the controller's Live data, "
                 + "step through the groups, and use Show on dashboard on the value you want.")
                .font(.callout)
                .foregroundStyle(Color.textMuted)
                .fixedSize(horizontal: false, vertical: true)
            Form {
                TextField("Name", text: $name)
                TextField("Controller (hex)", text: $address)
                Stepper("Group \(group)", value: $group, in: 0...255)
                Picker("Field", selection: $field) {
                    ForEach(1...4, id: \.self) { Text("\($0)").tag($0) }
                }
                .pickerStyle(.segmented)
            }
            HStack {
                Spacer()
                Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Save") {
                    dash.set(DashSlot(kind: .sensor, address: Int(address, radix: 16) ?? 1, group: group,
                                      field: field, name: name.isEmpty ? "Value" : name), at: index)
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
                .disabled(Int(address, radix: 16).map { !(1...0x7F).contains($0) } ?? true)
            }
        }
        .padding(24)
        .frame(width: 440)
        .onAppear {
            let s = dash.slots[index]
            guard s.kind == .sensor else { return }
            name = s.name
            address = String(format: "%02X", s.address)
            group = s.group
            field = s.field
        }
    }
}

/// Scales the estimate so the trip's fuel matches what the pump says.
private struct FuelCalibration: View {
    @EnvironmentObject var dash: Dashboard
    @Environment(\.dismiss) private var dismiss
    @State private var pumped = ""

    private var unit: String { dash.imperial ? "gal" : "l" }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Calibrate fuel economy").font(.title2.weight(.semibold))
            Text("Fill the tank and tap Reset trip. Drive with the dashboard running, then fill up again and "
                 + "enter the amount the pump shows. The estimate is scaled to match.")
                .font(.callout)
                .foregroundStyle(Color.textMuted)
                .fixedSize(horizontal: false, vertical: true)
            let used = dash.display(dash.fuel.trip_l, unit: "l")
            Text(String(format: "This trip's estimate: %.2f %@", used.value, used.unit))
                .monospacedDigit()
            TextField("Fuel pumped (\(unit))", text: $pumped)
            HStack {
                if dash.fuel.factor != 1 {
                    Button("Reset calibration") { dash.resetCalibration(); dismiss() }
                }
                Spacer()
                Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Calibrate") {
                    if let v = Double(pumped) { dash.calibrate(pumped: v) }
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
                .disabled((Double(pumped) ?? 0) <= 0 || dash.fuel.trip_l <= 0)
            }
        }
        .padding(24)
        .frame(width: 440)
    }
}
