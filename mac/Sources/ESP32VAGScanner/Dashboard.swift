import Foundation
import KWCore

/// One group read from kw_dash_run.
struct DashReading: Sendable {
    let address: Int
    let group: Int
    let err: Int32
    let values: [SensorValue]
    let ident: String
}

/// What a dashboard slot shows (kwcore/dashboard.h KwDashSlot).
struct DashSlot: Codable, Hashable {
    enum Kind: Int, Codable { case empty, sensor, fuel }
    var kind: Kind
    var address: Int
    var group: Int
    var field: Int
    var name: String

    static let empty = DashSlot(kind: .empty, address: 0, group: 0, field: 0, name: "")

    init(kind: Kind, address: Int, group: Int, field: Int, name: String) {
        self.kind = kind
        self.address = address
        self.group = group
        self.field = field
        self.name = name
    }

    init(_ preset: KwDashPreset) {
        self.init(kind: Kind(rawValue: Int(preset.kind)) ?? .empty, address: Int(preset.address),
                  group: Int(preset.group), field: Int(preset.field), name: String(cString: preset.name))
    }

    /// "01 · Group 4 · Field 3"
    var source: String {
        switch kind {
        case .empty: return ""
        case .fuel: return String(format: "%02X · Groups %d + %d", KW_FUEL_ADDRESS, KW_FUEL_MAF_GROUP, KW_FUEL_SPEED_GROUP)
        case .sensor: return String(format: "%02X · Group %d · Field %d", address, group, field)
        }
    }

    /// The preset's caveat when the slot sits where an unconfirmed preset put it.
    var note: String? {
        DashSlot.presets.first { $0.slot.kind == kind && $0.slot.address == address
            && $0.slot.group == group && $0.slot.field == field }?.note
    }

    static let presets: [(slot: DashSlot, note: String?)] = {
        var list: [(DashSlot, String?)] = []
        var i: Int32 = 0
        while let p = kw_dash_preset(i) {
            list.append((DashSlot(p.pointee), p.pointee.note.map { String(cString: $0) }))
            i += 1
        }
        return list
    }()

    static let defaults = Array(presets.prefix(Int(KW_DASH_SLOTS)).map(\.slot))
}

struct DashSample: Identifiable {
    let time: Date
    /// Metric, as kwcore decodes it; converted for display.
    let value: Double
    var id: Date { time }
}

/// Live state of one slot.
struct DashSlotState {
    var value: Double?
    var unit = ""
    /// Shown when the field isn't a number (status bits, text).
    var text: String?
    var label: String?
    var history: [DashSample] = []
    var error: String?
}

/// The four-slot dashboard: layout, readings, trends and the fuel estimate.
@MainActor
final class Dashboard: ObservableObject {
    @Published var slots: [DashSlot] {
        didSet { save() }
    }
    @Published private(set) var states = Array(repeating: DashSlotState(), count: Int(KW_DASH_SLOTS))
    @Published private(set) var fuel = KwFuel()
    @Published var imperial: Bool {
        didSet { UserDefaults.standard.set(imperial, forKey: "dashImperial") }
    }
    @Published var running = false
    @Published var status = ""

    static let historyLimit: TimeInterval = 15 * 60
    private var stop: StopFlag?

    init() {
        if let data = UserDefaults.standard.data(forKey: "dashSlots"),
           let saved = try? JSONDecoder().decode([DashSlot].self, from: data), saved.count == Int(KW_DASH_SLOTS) {
            slots = saved
        } else {
            slots = DashSlot.defaults
        }
        imperial = UserDefaults.standard.object(forKey: "dashImperial") as? Bool ?? true
        let factor = UserDefaults.standard.double(forKey: "fuelFactor")
        kw_fuel_init(&fuel, factor > 0 ? factor : 1)
        fuel.trip_l = UserDefaults.standard.double(forKey: "fuelTripL")
        fuel.trip_km = UserDefaults.standard.double(forKey: "fuelTripKm")
    }

    var plan: [KwDashRead] {
        var layout = KwDashLayout()
        withUnsafeMutablePointer(to: &layout.slot) { tuple in
            tuple.withMemoryRebound(to: KwDashSlot.self, capacity: Int(KW_DASH_SLOTS)) { slot in
                for (i, s) in slots.enumerated() {
                    slot[i].kind = UInt8(s.kind.rawValue)
                    slot[i].address = UInt8(s.address)
                    slot[i].group = UInt8(s.group)
                    slot[i].field = UInt8(s.field)
                }
            }
        }
        var plan = [KwDashRead](repeating: KwDashRead(), count: Int(KW_DASH_PLAN_MAX))
        let n = kw_dash_plan(&layout, &plan, KW_DASH_PLAN_MAX)
        return Array(plan.prefix(Int(n)))
    }

    func set(_ slot: DashSlot, at index: Int) {
        slots[index] = slot
        states[index] = DashSlotState()
        if running { restart = true; stop?.stop() }
    }

    /// The layout changed while live: start again on the new plan once the bus is free.
    private var restart = false

    func start(model: AppModel) {
        guard !model.busy, !running else { return }
        let plan = plan
        guard !plan.isEmpty else { status = "Choose something to show first."; return }
        let stop = StopFlag()
        self.stop = stop
        running = true
        model.busy = true
        status = "Connecting…"
        Task {
            do {
                try await model.kline.runDashboard(port: model.port, baud: model.baud, plan: plan, stop: stop) { reading in
                    Task { @MainActor in self.apply(reading) }
                }
                status = ""
            } catch {
                status = error.localizedDescription
            }
            running = false
            model.busy = false
            saveTrip()
            if restart {
                restart = false
                start(model: model)
            }
        }
    }

    func stopReading() {
        restart = false
        stop?.stop()
    }

    func resetTrip() {
        kw_fuel_reset_trip(&fuel)
        saveTrip()
    }

    /// `pumped` is in the display unit (gallons or litres).
    func calibrate(pumped: Double) {
        let litres = imperial ? pumped * 3.785411784 : pumped
        let factor = kw_fuel_calibrated_factor(&fuel, litres)
        // later samples use the new factor; scale the trip so the average reflects it now
        fuel.trip_l *= factor / fuel.factor
        fuel.factor = factor
        UserDefaults.standard.set(factor, forKey: "fuelFactor")
        saveTrip()
    }

    func resetCalibration() {
        fuel.trip_l /= fuel.factor
        fuel.factor = 1
        UserDefaults.standard.removeObject(forKey: "fuelFactor")
        saveTrip()
    }

    // MARK: Readings

    private func apply(_ r: DashReading) {
        let now = Date()
        status = "Live"
        if r.address == Int(KW_FUEL_ADDRESS) && r.err == KW_OK {
            if r.group == Int(KW_FUEL_MAF_GROUP), let maf = number(r, field: Int(KW_FUEL_MAF_FIELD)) {
                kw_fuel_maf(&fuel, maf.value, now.timeIntervalSinceReferenceDate)
            }
            if r.group == Int(KW_FUEL_SPEED_GROUP), let speed = number(r, field: Int(KW_FUEL_SPEED_FIELD)) {
                kw_fuel_speed(&fuel, speed.value, now.timeIntervalSinceReferenceDate)
            }
        }

        for (i, slot) in slots.enumerated() {
            var state = states[i]
            switch slot.kind {
            case .empty:
                continue
            case .sensor:
                guard slot.address == r.address && slot.group == r.group else { continue }
                if r.err != KW_OK {
                    state.error = Self.describe(r.err, slot)
                } else if slot.field > r.values.count {
                    state.error = "Group \(slot.group) has only \(r.values.count) fields."
                } else {
                    var sv = r.values[slot.field - 1]
                    state.error = nil
                    state.label = String(cString: kw_label(r.ident, Int32(slot.group), Int32(slot.field), sv.sensor_id))
                    if let n = number(r, field: slot.field) {
                        state.value = n.value
                        state.unit = n.unit
                        state.text = nil
                        state.history.append(DashSample(time: now, value: n.value))
                    } else {
                        var buf = [CChar](repeating: 0, count: 64)
                        kw_format_sensor(&sv, &buf, 64)
                        state.text = String(cString: buf)
                    }
                }
            case .fuel:
                guard r.address == Int(KW_FUEL_ADDRESS)
                      && (r.group == Int(KW_FUEL_MAF_GROUP) || r.group == Int(KW_FUEL_SPEED_GROUP)) else { continue }
                if r.err != KW_OK {
                    state.error = Self.describe(r.err, slot)
                    break
                }
                state.error = nil
                let instant = kw_fuel_instant(&fuel)
                state.value = instant > 0 ? instant : nil
                if instant > 0 && r.group == Int(KW_FUEL_SPEED_GROUP) {
                    state.history.append(DashSample(time: now, value: instant))
                }
            }
            state.history.removeAll { now.timeIntervalSince($0.time) > Self.historyLimit }
            states[i] = state
        }
    }

    private func number(_ r: DashReading, field: Int) -> (value: Double, unit: String)? {
        guard field >= 1 && field <= r.values.count else { return nil }
        var sv = r.values[field - 1]
        var value = 0.0
        var unit: UnsafePointer<CChar>?
        guard kw_sensor_number(&sv, &value, &unit) != 0 else { return nil }
        return (value, unit.map { String(cString: $0) } ?? "")
    }

    private static func describe(_ err: Int32, _ slot: DashSlot) -> String {
        let address = String(format: "%02X", slot.kind == .fuel ? Int(KW_FUEL_ADDRESS) : slot.address)
        switch err {
        case KW_ENOWAKE: return "No response from \(address). Retrying."
        case KW_ENAK: return "\(address) has no group \(slot.group). Pick another in Live data."
        case KW_EKWP2000: return "\(address) uses KWP2000, which isn't supported."
        default: return String(cString: kw_strerror(err)).capitalized
        }
    }

    // MARK: Display

    /// A metric value and unit as shown, e.g. (194, "°F").
    func display(_ value: Double, unit: String) -> (value: Double, unit: String) {
        unit.withCString { metric in
            var shown: UnsafePointer<CChar>?
            let v = kw_unit_convert(value, metric, imperial ? 1 : 0, &shown)
            // an unconverted unit comes back as our own temporary C string
            return (v, shown.map { $0 == metric ? unit : String(cString: $0) } ?? unit)
        }
    }

    /// Fuel economy in the display unit: mpg (US) or l/100km. Coasting reads
    /// near-infinite, so it tops out at 99.9 mpg (2.4 l/100km) like a trip computer.
    func economy(_ lPer100km: Double) -> Double {
        let l = max(lPer100km, 235.214583 / 99.9)
        return imperial ? 235.214583 / l : l
    }

    var economyUnit: String { imperial ? "mpg" : "l/100km" }

    // MARK: Persistence

    private func save() {
        if let data = try? JSONEncoder().encode(slots) {
            UserDefaults.standard.set(data, forKey: "dashSlots")
        }
    }

    private func saveTrip() {
        UserDefaults.standard.set(fuel.trip_l, forKey: "fuelTripL")
        UserDefaults.standard.set(fuel.trip_km, forKey: "fuelTripKm")
    }
}
