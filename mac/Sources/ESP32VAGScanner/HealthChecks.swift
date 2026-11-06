import Foundation
import KWCore

func healthText<T>(_ value: T) -> String {
    var value = value
    return withUnsafeBytes(of: &value) { bytes in
        String(cString: bytes.bindMemory(to: CChar.self).baseAddress!)
    }
}

struct HealthReport: Codable, Identifiable {
    var id = UUID()
    let date: Date
    let check: String
    let controller: String
    let demo: Bool
    let status: String
    let message: String
    let readings: String
    let confirmations: [String]
    let source: String
    let physicalObservation: String?
}

@MainActor
final class HealthChecks: ObservableObject {
    @Published var check = 0
    @Published var confirmations: UInt32 = 0
    @Published private(set) var running = false
    @Published private(set) var result = KwHealthResult()
    @Published private(set) var controller = ""
    @Published private(set) var sampledAt: Date?
    @Published private(set) var status = "Choose a check, review its prerequisites, then Start."
    @Published private(set) var reports: [HealthReport] = []
    private var state = KwHealthState()
    private var frame: KwHealthFrame?
    private var receivedUptime = 0.0
    private var stop: StopFlag?
    private var runID = UUID()
    private var demo = false

    var procedure: KwHealthProcedure { kw_health_procedure(Int32(check))!.pointee }
    var confirmationLabels: [String] {
        let p = procedure
        return [p.confirmation.0, p.confirmation.1, p.confirmation.2, p.confirmation.3]
            .compactMap { $0.map { String(cString: $0) } }
    }
    var resultStatus: String { String(cString: kw_health_status_name(result.status)) }

    init() {
        kw_health_init(&state, 0)
        result.status = Int32(KW_HEALTH_INCONCLUSIVE.rawValue)
        if let data = UserDefaults.standard.data(forKey: "eurovanHealthReports"),
           let saved = try? JSONDecoder().decode([HealthReport].self, from: data) {
            reports = saved
        }
    }

    func selectCheck() {
        guard !running else { return }
        confirmations = 0
        frame = nil
        controller = ""
        sampledAt = nil
        kw_health_init(&state, Int32(check))
        result = KwHealthResult()
        result.status = Int32(KW_HEALTH_INCONCLUSIVE.rawValue)
        status = "Review the prerequisites, then Start."
    }

    func start(model: AppModel) {
        guard !running, !model.busy else { return }
        kw_health_init(&state, Int32(check))
        confirmations = 0
        frame = nil
        sampledAt = nil
        controller = ""
        result = KwHealthResult()
        result.status = Int32(KW_HEALTH_INCONCLUSIVE.rawValue)
        running = true
        model.busy = true
        status = "Connecting and checking controller identity…"
        let id = UUID()
        runID = id
        let stop = StopFlag()
        self.stop = stop
        demo = KLine.isDemo(model.port)
        let (stream, continuation) = AsyncStream<KwHealthFrame>.makeStream()
        Task {
            let consumer = Task {
                for await f in stream {
                    guard self.runID == id, !stop.isStopped else { continue }
                    self.apply(f)
                }
            }
            do {
                try await model.kline.runHealth(port: model.port, baud: model.baud, check: check, stop: stop) { f in
                    continuation.yield(f)
                }
                continuation.finish()
                await consumer.value
                status = stop.isStopped ? "Stopped. Start again for live readiness."
                                       : "Read ended. Review the result, then Start to retry."
            } catch {
                continuation.finish()
                await consumer.value
                status = error.localizedDescription
            }
            running = false
            model.busy = false
            // Readiness is only actionable during a live run. Preserve measurements
            // for inspection, but never leave an old ATF 'Ready' banner active.
            if result.status == Int32(KW_HEALTH_READY.rawValue) {
                result.status = Int32(KW_HEALTH_INCONCLUSIVE.rawValue)
            }
        }
    }

    func stopReading() { stop?.stop() }

    private func apply(_ f: KwHealthFrame) {
        frame = f
        receivedUptime = ProcessInfo.processInfo.systemUptime
        sampledAt = Date()
        controller = healthText(f.ident)
        status = demo ? "Live simulation: parked vehicle, warm engine, clean simulated fault memory"
                      : "Live · procedure-based screening; new health groups await vehicle validation"
        reevaluate()
    }

    func reevaluate() {
        guard var frame else { return }
        let now = frame.finished_s + ProcessInfo.processInfo.systemUptime - receivedUptime
        kw_health_evaluate(&state, &frame, confirmations, now, &result)
    }

    func save(physicalObservation: String? = nil) {
        reevaluate()
        guard let date = sampledAt else { return }
        if physicalObservation != nil {
            guard check == Int(KW_HEALTH_ATF.rawValue), running,
                  result.status == Int32(KW_HEALTH_READY.rawValue) else { return }
        }
        reports.insert(HealthReport(date: date, check: String(cString: procedure.title),
            controller: controller, demo: demo, status: resultStatus,
            message: healthText(result.message), readings: healthText(result.readings),
            confirmations: confirmationLabels.enumerated().compactMap { i, label in
                confirmations & (1 << i) != 0 ? label : nil
            }, source: String(cString: procedure.source_url), physicalObservation: physicalObservation), at: 0)
        reports = Array(reports.prefix(30))
        if let data = try? JSONEncoder().encode(reports) {
            UserDefaults.standard.set(data, forKey: "eurovanHealthReports")
        }
    }
}
